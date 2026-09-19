#include "Backend/platform/VulkanPlatform.h"

#include <cstring>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "../VkDef.h"
#include "../VkUtils.h"
#include "../VulkanContext.h"
#include "../VulkanDriver.h"
#include "VulkanPlatformSwapChainImpl.h"
#include "vulkan/core/VulInstance.h"
#include "vulkan/core/VulLogicDevice.h"
#include "vulkan/core/VulPhysicalDevice.h"
#include "vulkan/core/VulQueue.h"

BEGIN_NS_BACKEND

namespace {

using ExtensionSet = VulkanPlatform::ExtensionSet;

bool shouldSkipFormat(VkFormat format) {
    // 跳过需要扩展支持的格式
    for (VkFormat const extFormat : EXT_VK_FORMATS) {
        if (format == extFormat) {
            return true;
        }
    }
    return false;
}

void printDeviceInfo(VulInstancePtr const &instance, VulPhysicalDevicePtr const &device) {
    // 打印驱动或 MoltenVK 信息（如可用）
    if (vkGetPhysicalDeviceProperties2) {
        VkPhysicalDeviceDriverProperties driverProperties = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
        };
        VkPhysicalDeviceProperties2 physicalDeviceProperties2 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        };
        VK_UTILS::chainStruct(&physicalDeviceProperties2, &driverProperties);
        vkGetPhysicalDeviceProperties2(device->GetHandle(), &physicalDeviceProperties2);
        LOG_INFO("Vulkan device driver: {} {}", driverProperties.driverName, driverProperties.driverInfo);
    }

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(device->GetHandle(), &deviceProperties);

    uint32_t const driverVersion = deviceProperties.driverVersion;
    uint32_t const vendorID      = deviceProperties.vendorID;
    uint32_t const deviceID      = deviceProperties.deviceID;
    int const      major         = VK_VERSION_MAJOR(deviceProperties.apiVersion);
    int const      minor         = VK_VERSION_MINOR(deviceProperties.apiVersion);

    std::vector<VkPhysicalDevice> const physicalDevices = VK_UTILS::enumerate(vkEnumeratePhysicalDevices, instance->GetHandle());

    LOG_INFO(
        "Selected physical device '{}' from {} physical devices. (vendor {:#x}, device {:#x}, "
        "driver {}, api {}.{})",
        deviceProperties.deviceName, physicalDevices.size(), vendorID, deviceID, driverVersion, major, minor);
}

#if BVK_ENABLED(BVK_DEBUG_VALIDATION)
void printDepthFormats(VulPhysicalDevicePtr const &device) {
    // 诊断用途：打印可用的深度格式
    constexpr VkFormatFeatureFlags required = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    LOG_INFO("Sampleable depth formats: ");
    for (VkFormat const format : ALL_VK_FORMATS) {
        // 跳过需要扩展支持的格式
        if (shouldSkipFormat(format)) {
            continue;
        }

        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(device->GetHandle(), format, &props);
        if ((props.optimalTilingFeatures & required) == required) {
            LOG_INFO("{}", static_cast<int>(format));
        }
    }
}
#endif

ExtensionSet getDeviceExtensions(VulPhysicalDevicePtr const &device) {
    ExtensionSet const TARGET_EXTS = {
#if BVK_ENABLED(BVK_DEBUG_DEBUG_UTILS)
        VK_EXT_DEBUG_MARKER_EXTENSION_NAME,
#endif
#if PLATFORM_ANDROID
        // 目前仅 Android 支持外部图像
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
        VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,
        VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,
        VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME,
        // 外部图像需要
        VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
#endif
#if PLATFORM_APPLE
        // MoltenVK 是唯一我们关注的非 conformant 实现
        VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME,
#endif
        VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME,
        VK_KHR_MULTIVIEW_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        // 动态渲染需要
        VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME,
        VK_KHR_GLOBAL_PRIORITY_EXTENSION_NAME,

#if BVK_ENABLED(BVK_DEBUG_SHADER_MODULE)
        VK_EXT_PIPELINE_CREATION_FEEDBACK_EXTENSION_NAME,
#endif
    };

    ExtensionSet exts;
    auto const   extensions = VK_UTILS::enumerate(vkEnumerateDeviceExtensionProperties, device->GetHandle(), static_cast<char const *>(nullptr));
    for (auto const &extension : extensions) {
        std::string name(reinterpret_cast<char const *>(extension.extensionName));

        // 规避 Adreno 上扩展名为 0 长度的 bug
        if (name.empty()) {
            continue;
        }

        if (TARGET_EXTS.contains(name)) {
            exts.insert(name);
        }
    }
    return exts;
}

std::tuple<ExtensionSet, ExtensionSet> pruneExtensions(VulPhysicalDevicePtr const &device, DriverConfig const &driverConfig,
                                                       ExtensionSet const &instExts, ExtensionSet const &deviceExts) noexcept {
    ExtensionSet newInstExts   = instExts;
    ExtensionSet newDeviceExts = deviceExts;

#if BVK_ENABLED(BVK_DEBUG_DEBUG_UTILS)
    // debugUtils 与 debugMarkers 扩展互斥
    if (newInstExts.contains(VK_EXT_DEBUG_UTILS_EXTENSION_NAME) && newInstExts.contains(VK_EXT_DEBUG_MARKER_EXTENSION_NAME)) {
        newDeviceExts.erase(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
    }
#endif

#if BVK_ENABLED(BVK_DEBUG_VALIDATION)
    // debugMarker 必须同时请求 debugReport 实例扩展；检查是否存在
    if (newInstExts.contains(VK_EXT_DEBUG_MARKER_EXTENSION_NAME) && !newInstExts.contains(VK_EXT_DEBUG_MARKER_EXTENSION_NAME)) {
        newDeviceExts.erase(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
    }
#endif

    if (driverConfig.stereoscopicType != StereoscopicType::Multiview) {
        newDeviceExts.erase(VK_KHR_MULTIVIEW_EXTENSION_NAME);
    }

    return std::tuple(newInstExts, newDeviceExts);
}

VkFormatList findAttachmentDepthStencilFormats(VulPhysicalDevicePtr const &device) {
    VkFormatFeatureFlags const features = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

    // 顺序表示 depth+stencil 格式的偏好
    constexpr VkFormat kFormats[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_X8_D24_UNORM_PACK32,

        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };
    VkFormatList selectedFormats;
    for (VkFormat format : kFormats) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(device->GetHandle(), format, &props);
        if ((props.optimalTilingFeatures & features) == features) {
            selectedFormats.push_back(format);
        }
    }
    return selectedFormats;
}

VkFormatList findBlittableDepthStencilFormats(VulPhysicalDevicePtr const &device) {
    VkFormatList                   selectedFormats;
    constexpr VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;

    for (VkFormat const format : ALL_VK_FORMATS) {
        // 跳过需要扩展支持的格式
        if (shouldSkipFormat(format)) {
            continue;
        }

        if (VK_UTILS::IsVkDepthFormat(format)) {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(device->GetHandle(), format, &props);
            if ((props.optimalTilingFeatures & required) == required) {
                selectedFormats.push_back(format);
            }
        }
    }
    return selectedFormats;
}

bool hasUnifiedMemoryArchitecture(VkPhysicalDeviceMemoryProperties memoryProperties) noexcept {
    for (uint32_t i = 0; i < memoryProperties.memoryHeapCount; ++i) {
        if ((memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0) {
            return false;
        }
    }
    return true;
}

}  // namespace

struct VulkanPlatformPrivate {
    VulInstancePtr       m_pInstance;
    VulPhysicalDevicePtr m_pPhysicalDevice;
    VulLogicDevicePtr    m_pDevice;
    VulQueuePtr          m_pGraphicsQueue;
    VulQueuePtr          m_pProtectedGraphicsQueue;
    VulkanContextPtr     m_pContext                          = {};
    uint32_t             m_graphicsQueueFamilyIndex          = INVALID_VK_INDEX;
    uint32_t             m_graphicsQueueIndex                = INVALID_VK_INDEX;
    uint32_t             m_protectedGraphicsQueueFamilyIndex = INVALID_VK_INDEX;
    uint32_t             m_protectedGraphicsQueueIndex       = INVALID_VK_INDEX;
    bool                 m_sharedContext                     = false;
};

VulkanPlatform::VulkanPlatform() noexcept  = default;
VulkanPlatform::~VulkanPlatform() noexcept = default;

VkInstance VulkanPlatform::GetVkInstance() const noexcept { return m_pImpl->m_pInstance->GetHandle(); }

VkPhysicalDevice VulkanPlatform::GetVkPhysicalDevice() const noexcept { return m_pImpl->m_pPhysicalDevice->GetHandle(); }

VkDevice VulkanPlatform::GetVkDevice() const noexcept { return m_pImpl->m_pDevice->GetHandle(); }

uint32_t VulkanPlatform::GetGraphicsQueueFamilyIndex() const noexcept { return m_pImpl->m_graphicsQueueFamilyIndex; }

uint32_t VulkanPlatform::GetGraphicsQueueIndex() const noexcept { return m_pImpl->m_graphicsQueueIndex; }

VkQueue VulkanPlatform::GetVkGraphicsQueue() const noexcept {
    return m_pImpl->m_pGraphicsQueue ? m_pImpl->m_pGraphicsQueue->GetHandle() : VK_NULL_HANDLE;
}

DriverPtr VulkanPlatform::CreateDriver(const DriverConfig &config, void *shareContext) {
    initRuntime(shareContext);

    m_pImpl->m_pContext = new VulkanContext();

    ExtensionSet instExts = initInstance();
    selectPhysicalDevice(shareContext);
    createLogicalDevice(config, instExts, shareContext);
    initQueues();

#if BVK_ENABLED(BVK_DEBUG_VALIDATION)
    printDepthFormats(m_pImpl->m_pPhysicalDevice);
#endif

    return VulkanDriver::Create(this, m_pImpl->m_pContext, config);
}

VulkanPlatform::SwapChainPtr VulkanPlatform::CreateSwapChain(void *nativeWindow, uint64_t flags, VkExtent2D extent) {
    // extent 非零即表示无窗口：此时只需虚拟尺寸，不创建 surface
    bool const headless = extent.width != 0 && extent.height != 0;
    if (headless) {
        return new VulkanPlatformHeadlessSwapChain(*m_pImpl->m_pContext, GetVkDevice(), GetVkGraphicsQueue(), extent, flags);
    }

    LOG_ASSERT(nativeWindow != nullptr);
    if ((flags & kSwapChainConfigProtectedContent) != 0 && !m_pImpl->m_pContext->IsProtectedMemorySupported()) {
        LOG_WARN("protected swapchain requested, but VulkanPlatform does not support it");
    }

    auto [surface, fallbackExtent] = CreateVkSurfaceKHR(nativeWindow, GetVkInstance(), flags);
    // surface 的所有权随交换链转移，由其析构时销毁
    return new VulkanPlatformSurfaceSwapChain(*m_pImpl->m_pContext, GetVkPhysicalDevice(), GetVkDevice(), GetVkGraphicsQueue(), GetVkInstance(),
                                              surface, fallbackExtent, flags);
}

VulkanPlatform::SwapChainBundle VulkanPlatform::GetSwapChainBundle(SwapChainPtr handle) {
    if (handle == nullptr) {
        return {};
    }
    return static_cast<VulkanPlatformSwapChainBase *>(handle)->GetSwapChainBundle();
}

VkResult VulkanPlatform::Acquire(SwapChainPtr handle, ImageSyncData *outImageSyncData) {
    if (handle == nullptr || outImageSyncData == nullptr) {
        return VK_ERROR_UNKNOWN;
    }
    return static_cast<VulkanPlatformSwapChainBase *>(handle)->Acquire(outImageSyncData);
}

VkResult VulkanPlatform::Present(SwapChainPtr handle, uint32_t index, VkSemaphore finishedDrawing) {
    if (handle == nullptr) {
        return VK_ERROR_UNKNOWN;
    }
    return static_cast<VulkanPlatformSwapChainBase *>(handle)->Present(index, finishedDrawing);
}

bool VulkanPlatform::HasResized(SwapChainPtr handle) {
    if (handle == nullptr) {
        return false;
    }
    return static_cast<VulkanPlatformSwapChainBase *>(handle)->HasResized();
}

bool VulkanPlatform::IsProtected(SwapChainPtr handle) {
    if (handle == nullptr) {
        return false;
    }
    return static_cast<VulkanPlatformSwapChainBase *>(handle)->IsProtected();
}

VkResult VulkanPlatform::Recreate(SwapChainPtr handle) {
    if (handle == nullptr) {
        return VK_ERROR_UNKNOWN;
    }
    return static_cast<VulkanPlatformSwapChainBase *>(handle)->Recreate();
}

void VulkanPlatform::Destroy(SwapChainPtr handle) { delete static_cast<VulkanPlatformSwapChainBase *>(handle); }

Platform::Sync *VulkanPlatform::CreateSync(std::shared_ptr<VulkanCmdFence> fenceStatus) noexcept {
    auto *sync        = new VulkanSync();
    sync->fenceStatus = std::move(fenceStatus);
    return sync;
}

void VulkanPlatform::DestroySync(Platform::Sync *sync) noexcept {
    // sync 必为本平台创建的对象；Platform::Sync 无虚析构，须按实际类型销毁以释放其成员
    delete static_cast<VulkanSync *>(sync);
}

void VulkanPlatform::Terminate() {
    // 交换链由驱动持有并负责销毁，平台只释放自己创建的实例/设备（共享对象由调用方释放）
    m_pImpl->m_pProtectedGraphicsQueue = {};
    m_pImpl->m_pGraphicsQueue          = {};
    m_pImpl->m_pDevice                 = {};
    m_pImpl->m_pPhysicalDevice         = {};
    m_pImpl->m_pInstance               = {};
}

VkInstance VulkanPlatform::CreateVkInstance(VkInstanceCreateInfo const &createInfo) noexcept {
    VkInstance instance = VK_NULL_HANDLE;
    CALL_VK(vkCreateInstance(&createInfo, kVkAlloc, &instance));
    return instance;
}

VkPhysicalDevice VulkanPlatform::SelectVkPhysicalDevice(VkInstance instance) noexcept {
    Customization::GPUPreference const pref = GetCustomization().gpu;
    return VulPhysicalDevice::Select(instance, pref.deviceName, pref.index);
}

VkDevice VulkanPlatform::CreateVkDevice(VkDeviceCreateInfo const &createInfo) noexcept {
    VkDevice device = VK_NULL_HANDLE;
    VkResult result = vkCreateDevice(GetVkPhysicalDevice(), &createInfo, kVkAlloc, &device);
    if (result != VK_SUCCESS) {
        LOG_CRITICAL("vkCreateDevice error={}", static_cast<int32_t>(result));
    }
    return device;
}

void VulkanPlatform::initRuntime(void *shareContext) {
    if (volkInitialize() != VK_SUCCESS) {
        LOG_CRITICAL("volkInitialize() failed");
    }

    if (shareContext) {
        VulkanSharedContext const *scontext = static_cast<VulkanSharedContext const *>(shareContext);
        LOG_ASSERT(scontext->instance != VK_NULL_HANDLE);
        LOG_ASSERT(scontext->physicalDevice != VK_NULL_HANDLE);
        LOG_ASSERT(scontext->logicalDevice != VK_NULL_HANDLE);
        LOG_ASSERT(scontext->graphicsQueueFamilyIndex != INVALID_VK_INDEX);
        LOG_ASSERT(scontext->graphicsQueueIndex != INVALID_VK_INDEX);

        // 共享的 instance / device 标记为共享，不负责销毁
        m_pImpl->m_pInstance       = new VulInstance(scontext->instance, true);
        m_pImpl->m_pPhysicalDevice = new VulPhysicalDevice(scontext->physicalDevice);
        m_pImpl->m_pDevice = new VulLogicDevice(scontext->logicalDevice, true, scontext->graphicsQueueFamilyIndex, scontext->graphicsQueueIndex,
                                                INVALID_VK_INDEX, INVALID_VK_INDEX);

        m_pImpl->m_sharedContext = true;
    }
}

ExtensionSet VulkanPlatform::initInstance() {
    ExtensionSet instExts;
    // 共享上下文时不假设任何扩展
    if (!m_pImpl->m_sharedContext) {
        // 包含平台所需的实例扩展（如 swapchain surface 扩展）
        auto const &swapchainExts = GetSwapchainInstanceExtensions();
        instExts                  = GetInstanceExtensions(swapchainExts);
        instExts.merge(GetRequiredInstanceExtensions());
    }
    if (!m_pImpl->m_pInstance) {
        m_pImpl->m_pInstance = VulInstance::Builder()
                                   .SetRequiredExtensions(instExts)
                                   .SetInstanceCreator([this](VkInstanceCreateInfo const &createInfo) { return CreateVkInstance(createInfo); })
                                   .Build();
    }
    LOG_ASSERT(m_pImpl->m_pInstance);

    // 加载实例级函数（及设备级函数的 dispatch table）
    volkLoadInstance(m_pImpl->m_pInstance->GetHandle());
    return instExts;
}

void VulkanPlatform::selectPhysicalDevice(void *shareContext) {
    Customization::GPUPreference const pref             = GetCustomization().gpu;
    bool const                         hasGPUPreference = pref.index >= 0 || !pref.deviceName.empty();
    LOG_ASSERT(!(hasGPUPreference && shareContext));

    if (!m_pImpl->m_pPhysicalDevice) {
        m_pImpl->m_pPhysicalDevice = new VulPhysicalDevice(SelectVkPhysicalDevice(m_pImpl->m_pInstance->GetHandle()));
    }
    LOG_ASSERT(m_pImpl->m_pPhysicalDevice);

    printDeviceInfo(m_pImpl->m_pInstance, m_pImpl->m_pPhysicalDevice);
}

ExtensionSet VulkanPlatform::initDeviceExtensions(DriverConfig const &config, ExtensionSet &instExts) {
    ExtensionSet deviceExts;
    // 共享上下文时不假设任何扩展
    if (!m_pImpl->m_sharedContext) {
        deviceExts                              = getDeviceExtensions(m_pImpl->m_pPhysicalDevice);
        auto [prunedInstExts, prunedDeviceExts] = pruneExtensions(m_pImpl->m_pPhysicalDevice, config, instExts, deviceExts);
        instExts                                = prunedInstExts;
        deviceExts                              = prunedDeviceExts;
    }
    return deviceExts;
}

void VulkanPlatform::createLogicalDevice(DriverConfig const &config, ExtensionSet &instExts, void *shareContext) {
    VulkanContext &context = *m_pImpl->m_pContext;

    // 设备扩展收集 + 特性查询
    ExtensionSet deviceExts = initDeviceExtensions(config, instExts);
    queryAndSetDeviceFeatures(config, instExts, deviceExts, shareContext);

    if (!m_pImpl->m_pDevice) {
        MiscDeviceFeatures requestedFeatures{};

        if (deviceExts.contains(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
            requestedFeatures.dynamicRendering = context.m_dynamicRenderingFeatures.dynamicRendering == VK_TRUE;
        }

        if (deviceExts.contains(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
            requestedFeatures.imageView2Don3DImage = context.m_portabilitySubsetFeatures.imageView2DOn3DImage == VK_TRUE;
        }

        if (context.IsGlobalPrioritySupported()) {
            requestedFeatures.gpuContextPriority = config.gpuContextPriority;
        }

        // 逻辑设备侧的构造参数同构但归属私有层，此处显式转换以维持分层
        VulLogicDevice::ExtraDeviceFeatures const deviceFeatures = {
            .dynamicRendering     = requestedFeatures.dynamicRendering,
            .imageView2Don3DImage = requestedFeatures.imageView2Don3DImage,
            .priority             = requestedFeatures.gpuContextPriority,
        };

        m_pImpl->m_pDevice = VulLogicDevice::Builder()
                                 .SetPhysicalDevice(m_pImpl->m_pPhysicalDevice)
                                 .SetDeviceExtensions(deviceExts)
                                 .SetFeatures(context.m_physicalDeviceFeatures.features)
                                 .SetVulkan11Features(context.m_physicalDeviceVk11Features)
                                 .SetProtectedQueue(context.IsProtectedMemorySupported())
                                 .SetRequestedFeatures(deviceFeatures)
                                 .SetDeviceCreator([this](VkDeviceCreateInfo const &createInfo) { return CreateVkDevice(createInfo); })
                                 .Build();
    }
}

void VulkanPlatform::initQueues() {
    // 从设备对象取回队列索引
    m_pImpl->m_graphicsQueueFamilyIndex          = m_pImpl->m_pDevice->GetGraphicsQueueFamilyIndex();
    m_pImpl->m_graphicsQueueIndex                = m_pImpl->m_pDevice->GetGraphicsQueueIndex();
    m_pImpl->m_protectedGraphicsQueueFamilyIndex = m_pImpl->m_pDevice->GetProtectedGraphicsQueueFamilyIndex();
    m_pImpl->m_protectedGraphicsQueueIndex       = m_pImpl->m_pDevice->GetProtectedGraphicsQueueIndex();

    LOG_ASSERT(m_pImpl->m_pDevice);
    LOG_ASSERT(m_pImpl->m_graphicsQueueFamilyIndex != INVALID_VK_INDEX);
    LOG_ASSERT(m_pImpl->m_graphicsQueueIndex != INVALID_VK_INDEX);

    m_pImpl->m_pGraphicsQueue = VulQueue::Builder()
                                    .SetDevice(m_pImpl->m_pDevice)
                                    .SetQueueFamilyIndex(m_pImpl->m_graphicsQueueFamilyIndex)
                                    .SetQueueIndex(m_pImpl->m_graphicsQueueIndex)
                                    .Build();
    LOG_ASSERT(m_pImpl->m_pGraphicsQueue);

    if (m_pImpl->m_protectedGraphicsQueueFamilyIndex != INVALID_VK_INDEX) {
        LOG_ASSERT(m_pImpl->m_protectedGraphicsQueueIndex != INVALID_VK_INDEX);
        m_pImpl->m_pProtectedGraphicsQueue = VulQueue::Builder()
                                                 .SetDevice(m_pImpl->m_pDevice)
                                                 .SetQueueFamilyIndex(m_pImpl->m_protectedGraphicsQueueFamilyIndex)
                                                 .SetQueueIndex(m_pImpl->m_protectedGraphicsQueueIndex)
                                                 .SetProtected(true)
                                                 .Build();
        LOG_ASSERT(m_pImpl->m_pProtectedGraphicsQueue);
    }
}

VulkanPlatform::ExtensionSet VulkanPlatform::GetInstanceExtensions(ExtensionSet const &externallyRequiredExts) {
    ExtensionSet const TARGET_EXTS = {
        VK_KHR_SURFACE_EXTENSION_NAME,

#if BVK_ENABLED(BVK_DEBUG_DEBUG_UTILS)
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
#if PLATFORM_APPLE
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
#endif
#if BVK_ENABLED(BVK_DEBUG_VALIDATION)
        VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
#endif
    };

    std::vector<VkExtensionProperties> const availableExts =
        VK_UTILS::enumerate(vkEnumerateInstanceExtensionProperties, static_cast<char const *>(nullptr));

    ExtensionSet exts;
    for (auto const &extension : availableExts) {
        std::string name(reinterpret_cast<char const *>(extension.extensionName));

        if (name.empty()) {
            continue;
        }

        if (TARGET_EXTS.contains(name) || externallyRequiredExts.contains(name)) {
            exts.insert(name);
        }
    }
    return exts;
}

void VulkanPlatform::queryAndSetDeviceFeatures(DriverConfig const &driverConfig, ExtensionSet const &instExts, ExtensionSet const &deviceExts,
                                               void *sharedContext) noexcept {
    VulkanContext &context = *m_pImpl->m_pContext;

    VkPhysicalDeviceProtectedMemoryFeatures queryProtectedMemoryFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES,
    };
    // 将查询结构链入 context 中的 physicalDeviceFeatures
    VK_UTILS::chainStruct(&context.m_physicalDeviceFeatures, &queryProtectedMemoryFeatures);
    VK_UTILS::chainStruct(&context.m_physicalDeviceFeatures, &context.m_physicalDeviceVk11Features);

    if (deviceExts.contains(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
        VK_UTILS::chainStruct(&context.m_physicalDeviceFeatures, &context.m_dynamicRenderingFeatures);
    }

    if (deviceExts.contains(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
        // 非 conformant 实现，需确认所需特性可用
        VK_UTILS::chainStruct(&context.m_physicalDeviceFeatures, &context.m_portabilitySubsetFeatures);
    }

    VkPhysicalDeviceGlobalPriorityQueryFeaturesKHR globalPriorityFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GLOBAL_PRIORITY_QUERY_FEATURES_KHR,
    };
    if (deviceExts.contains(VK_KHR_GLOBAL_PRIORITY_EXTENSION_NAME)) {
        VK_UTILS::chainStruct(&context.m_physicalDeviceFeatures, &globalPriorityFeatures);
    }

    if (vkGetPhysicalDeviceProperties2) {
        VK_UTILS::chainStruct(&context.m_physicalDeviceProperties, &context.m_driverProperties);
        context.m_driverPropertiesSupported = true;
    }

    // 初始化 physicalDeviceProperties / memoryProperties / physicalDeviceFeatures
    if (vkGetPhysicalDeviceProperties2) {
        vkGetPhysicalDeviceProperties2(m_pImpl->m_pPhysicalDevice->GetHandle(), &context.m_physicalDeviceProperties);
    } else {
        vkGetPhysicalDeviceProperties(m_pImpl->m_pPhysicalDevice->GetHandle(), &context.m_physicalDeviceProperties.properties);
    }

    if (vkGetPhysicalDeviceFeatures2) {
        vkGetPhysicalDeviceFeatures2(m_pImpl->m_pPhysicalDevice->GetHandle(), &context.m_physicalDeviceFeatures);
    } else {
        vkGetPhysicalDeviceFeatures(m_pImpl->m_pPhysicalDevice->GetHandle(), &context.m_physicalDeviceFeatures.features);
    }

    vkGetPhysicalDeviceMemoryProperties(m_pImpl->m_pPhysicalDevice->GetHandle(), &context.m_memoryProperties);

    // 在 context 中记录扩展支持情况
    if (sharedContext) {
        VulkanSharedContext const *scontext = static_cast<VulkanSharedContext const *>(sharedContext);
        context.m_debugUtilsSupported       = scontext->debugUtilsSupported;
        context.m_debugMarkersSupported     = scontext->debugMarkersSupported;
    } else {
        context.m_debugUtilsSupported               = instExts.contains(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        context.m_debugMarkersSupported             = deviceExts.contains(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
        context.m_pipelineCreationFeedbackSupported = deviceExts.contains(VK_EXT_PIPELINE_CREATION_FEEDBACK_EXTENSION_NAME);
        context.m_vertexInputDynamicStateSupported  = deviceExts.contains(VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME);
        context.m_globalPrioritySupported           = globalPriorityFeatures.globalPriorityQuery == VK_TRUE;
    }

    // 传递驱动配置（特性标志）
    context.m_asyncPipelineCachePrewarmingEnabled = driverConfig.vulkanEnableAsyncPipelineCachePrewarming;
    context.m_parallelShaderCompileDisabled       = driverConfig.disableParallelShaderCompile;
    context.m_stagingBufferBypassEnabled          = driverConfig.vulkanEnableStagingBufferBypass;

    context.m_protectedMemorySupported = static_cast<bool>(queryProtectedMemoryFeatures.protectedMemory);

    // 仅 instanced 立体渲染需要 shaderClipDistance
    if (driverConfig.stereoscopicType != StereoscopicType::Instanced) {
        context.m_physicalDeviceFeatures.features.shaderClipDistance = VK_FALSE;
    }

    // 检查懒分配内存的可用性
    context.m_lazilyAllocatedMemorySupported = false;
    for (uint32_t i = 0, typeCount = context.m_memoryProperties.memoryTypeCount; i < typeCount; ++i) {
        VkMemoryType const type = context.m_memoryProperties.memoryTypes[i];
        if (type.propertyFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) {
            context.m_lazilyAllocatedMemorySupported = true;
            break;
        }
    }

    context.m_depthClampSupported     = context.m_physicalDeviceFeatures.features.depthClamp == VK_TRUE;
    context.m_clipDistanceSupported   = context.m_physicalDeviceFeatures.features.shaderClipDistance == VK_TRUE;
    context.m_imageCubeArraySupported = context.m_physicalDeviceFeatures.features.imageCubeArray == VK_TRUE;

    context.m_isUnifiedMemoryArchitecture  = hasUnifiedMemoryArchitecture(context.m_memoryProperties);
    context.m_depthStencilFormats          = findAttachmentDepthStencilFormats(m_pImpl->m_pPhysicalDevice);
    context.m_blittableDepthStencilFormats = findBlittableDepthStencilFormats(m_pImpl->m_pPhysicalDevice);

    // 预热的 YCbCr 格式来自平台定制项：只有平台知道自己的外部图像编码方式
    context.m_pipelineCachePrewarmExternalFormats = GetCustomization().pipelineCachePrewarmExternalFormats;
}

END_NS_BACKEND