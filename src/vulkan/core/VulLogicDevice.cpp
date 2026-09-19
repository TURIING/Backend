#include "VulLogicDevice.h"

#include "Utils/Log.h"
#include "Utils/Macro.h"

#include <vector>

#include "../VkDef.h"
#include "../VkUtils.h"

BEGIN_NS_BACKEND

namespace {

VkQueueGlobalPriorityKHR TransGpuContextPriorityToVkQueueGlobalPriority(GpuContextPriority priority) {
    switch (priority) {
        CASE_FROM_TO(GpuContextPriority::Low, VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR);
        CASE_FROM_TO(GpuContextPriority::Medium, VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR);
        CASE_FROM_TO(GpuContextPriority::High, VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR);
        CASE_FROM_TO(GpuContextPriority::Realtime, VK_QUEUE_GLOBAL_PRIORITY_REALTIME_KHR);
        CASE_FROM_TO(GpuContextPriority::Default, VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR);
    }
    return VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR;
}

}  // namespace

struct VulLogicDevice::BuilderDetails {
    VulPhysicalDevicePtr             m_physicalDevice;
    std::unordered_set<std::string>  m_deviceExtensions;
    VkPhysicalDeviceFeatures         m_features       = {};
    VkPhysicalDeviceVulkan11Features m_vk11Features   = {};
    bool                             m_protectedQueue = false;
    ExtraDeviceFeatures              m_requestedFeatures;
    DeviceCreator                    m_deviceCreator;
};

VulLogicDevice::Builder::Builder() noexcept  = default;
VulLogicDevice::Builder::~Builder() noexcept = default;

VulLogicDevice::VulLogicDevice(VkDevice device, bool shared, uint32_t graphicsQueueFamilyIndex, uint32_t graphicsQueueIndex,
                               uint32_t protectedGraphicsQueueFamilyIndex, uint32_t protectedGraphicsQueueIndex)
    : m_graphicsQueueFamilyIndex(graphicsQueueFamilyIndex),
      m_graphicsQueueIndex(graphicsQueueIndex),
      m_protectedGraphicsQueueFamilyIndex(protectedGraphicsQueueFamilyIndex),
      m_protectedGraphicsQueueIndex(protectedGraphicsQueueIndex),
      m_shared(shared) {
    m_pHandle = device;
}

VulLogicDevice::~VulLogicDevice() {
    // 共享设备由调用方管理生命周期，不在此销毁
    if (!m_shared && m_pHandle != VK_NULL_HANDLE) {
        vkDestroyDevice(m_pHandle, nullptr);
    }
}

VulLogicDevice::Builder &VulLogicDevice::Builder::SetPhysicalDevice(VulPhysicalDevicePtr device) noexcept {
    m_pImpl->m_physicalDevice = std::move(device);
    return *this;
}

VulLogicDevice::Builder &VulLogicDevice::Builder::SetDeviceExtensions(std::unordered_set<std::string> const &exts) noexcept {
    m_pImpl->m_deviceExtensions = exts;
    return *this;
}

VulLogicDevice::Builder &VulLogicDevice::Builder::SetFeatures(VkPhysicalDeviceFeatures const &features) noexcept {
    m_pImpl->m_features = features;
    return *this;
}

VulLogicDevice::Builder &VulLogicDevice::Builder::SetVulkan11Features(VkPhysicalDeviceVulkan11Features const &features) noexcept {
    m_pImpl->m_vk11Features = features;
    return *this;
}

VulLogicDevice::Builder &VulLogicDevice::Builder::SetProtectedQueue(bool enabled) noexcept {
    m_pImpl->m_protectedQueue = enabled;
    return *this;
}

VulLogicDevice::Builder &VulLogicDevice::Builder::SetRequestedFeatures(ExtraDeviceFeatures const &features) noexcept {
    m_pImpl->m_requestedFeatures = features;
    return *this;
}

VulLogicDevice::Builder &VulLogicDevice::Builder::SetDeviceCreator(DeviceCreator creator) noexcept {
    m_pImpl->m_deviceCreator = std::move(creator);
    return *this;
}

VulLogicDevicePtr VulLogicDevice::Builder::Build() {
    // 识别并选择所需队列
    uint32_t graphicsQueueFamilyIndex = VK_UTILS::IdentifyGraphicsQueueFamilyIndex(m_pImpl->m_physicalDevice->GetHandle(), VK_QUEUE_GRAPHICS_BIT);
    LOG_ASSERT(graphicsQueueFamilyIndex != INVALID_VK_INDEX);
    uint32_t graphicsQueueIndex = 0;

    uint32_t protectedGraphicsQueueFamilyIndex = INVALID_VK_INDEX;
    uint32_t protectedGraphicsQueueIndex       = INVALID_VK_INDEX;
    if (m_pImpl->m_protectedQueue) {
        protectedGraphicsQueueFamilyIndex =
            VK_UTILS::IdentifyGraphicsQueueFamilyIndex(m_pImpl->m_physicalDevice->GetHandle(), (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_PROTECTED_BIT));
        protectedGraphicsQueueIndex = 0;
    }

    float              queuePriority[]  = { 1.0f };
    VkDeviceCreateInfo deviceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    };

    std::vector<const char *> requestExtensions;
    requestExtensions.reserve(m_pImpl->m_deviceExtensions.size() + 1);
    requestExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    for (auto const &ext : m_pImpl->m_deviceExtensions) {
        requestExtensions.push_back(ext.data());
    }

    bool const                               requiresGpuPriority     = m_pImpl->m_requestedFeatures.priority != GpuContextPriority::Default;
    VkDeviceQueueGlobalPriorityCreateInfoKHR queuePriorityCreateInfo = {
        .sType          = VK_STRUCTURE_TYPE_DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR,
        .globalPriority = TransGpuContextPriorityToVkQueueGlobalPriority(m_pImpl->m_requestedFeatures.priority),
    };

    VkDeviceQueueCreateInfo deviceQueueCreateInfo[2] = {};
    deviceQueueCreateInfo[0]                         = {
                                .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                .pNext            = requiresGpuPriority ? &queuePriorityCreateInfo : nullptr,
                                .queueFamilyIndex = graphicsQueueFamilyIndex,
                                .queueCount       = 1,
                                .pQueuePriorities = &queuePriority[0],
    };
    // protected 队列
    deviceQueueCreateInfo[1]       = deviceQueueCreateInfo[0];
    deviceQueueCreateInfo[1].flags = VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT;

    bool const hasProtectedQueue          = protectedGraphicsQueueFamilyIndex != INVALID_VK_INDEX;
    deviceCreateInfo.queueCreateInfoCount = hasProtectedQueue ? 2 : 1;
    deviceCreateInfo.pQueueCreateInfos    = deviceQueueCreateInfo;

    VkPhysicalDeviceFeatures enabledFeatures = {
        .depthClamp             = m_pImpl->m_features.depthClamp,
        .samplerAnisotropy      = m_pImpl->m_features.samplerAnisotropy,
        .textureCompressionETC2 = m_pImpl->m_features.textureCompressionETC2,
        .textureCompressionBC   = m_pImpl->m_features.textureCompressionBC,
        .shaderClipDistance     = m_pImpl->m_features.shaderClipDistance,
    };

    VkPhysicalDeviceFeatures2 enabledFeatures2 = {
        .sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .features = enabledFeatures,
    };
    VK_UTILS::chainStruct(&deviceCreateInfo, &enabledFeatures2);

    VkPhysicalDeviceVulkan11Features enabledVk11Features = {
        .sType     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .multiview = m_pImpl->m_vk11Features.multiview,
    };
    VK_UTILS::chainStruct(&deviceCreateInfo, &enabledVk11Features);

    deviceCreateInfo.enabledExtensionCount   = (uint32_t)requestExtensions.size();
    deviceCreateInfo.ppEnabledExtensionNames = requestExtensions.data();

    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRendering = {
        .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR,
        .dynamicRendering = m_pImpl->m_requestedFeatures.dynamicRendering ? VK_TRUE : VK_FALSE,
    };
    if (m_pImpl->m_deviceExtensions.contains(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
        VK_UTILS::chainStruct(&deviceCreateInfo, &dynamicRendering);
    }

    VkPhysicalDevicePortabilitySubsetFeaturesKHR portability = {
        .sType                     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR,
        .imageViewFormatSwizzle    = VK_TRUE,
        .imageView2DOn3DImage      = m_pImpl->m_requestedFeatures.imageView2Don3DImage ? VK_TRUE : VK_FALSE,
        .mutableComparisonSamplers = VK_TRUE,
    };
    if (m_pImpl->m_deviceExtensions.contains(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
        VK_UTILS::chainStruct(&deviceCreateInfo, &portability);
    }

    VkPhysicalDeviceMultiviewFeaturesKHR multiview = {
        .sType                       = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES_KHR,
        .multiview                   = m_pImpl->m_vk11Features.multiview,
        .multiviewGeometryShader     = VK_FALSE,
        .multiviewTessellationShader = VK_FALSE,
    };
    if (m_pImpl->m_deviceExtensions.contains(VK_KHR_MULTIVIEW_EXTENSION_NAME)) {
        VK_UTILS::chainStruct(&deviceCreateInfo, &multiview);
    }

    VkPhysicalDeviceProtectedMemoryFeatures protectedMemory = {
        .sType           = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES,
        .protectedMemory = VK_TRUE,
    };
    if (hasProtectedQueue) {
        // 请求时启用 protected memory
        VK_UTILS::chainStruct(&deviceCreateInfo, &protectedMemory);
    }

    VkPhysicalDeviceGlobalPriorityQueryFeaturesKHR globalPriority = {
        .sType               = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GLOBAL_PRIORITY_QUERY_FEATURES_KHR,
        .globalPriorityQuery = VK_TRUE,
    };
    if (requiresGpuPriority) {
        VK_UTILS::chainStruct(&deviceCreateInfo, &globalPriority);
    }

    LOG_ASSERT(m_pImpl->m_deviceCreator);
    VkDevice device = m_pImpl->m_deviceCreator(deviceCreateInfo);

    return new VulLogicDevice(device, false, graphicsQueueFamilyIndex, graphicsQueueIndex, protectedGraphicsQueueFamilyIndex,
                              protectedGraphicsQueueIndex);
}

END_NS_BACKEND