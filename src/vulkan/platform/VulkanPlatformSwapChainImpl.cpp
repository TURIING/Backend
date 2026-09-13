#include "VulkanPlatformSwapChainImpl.h"

#include <algorithm>
#include <array>
#include <tuple>
#include <vector>

#include "../VkDef.h"
#include "../VkUtils.h"

BEGIN_NS_BACKEND

namespace {

std::tuple<VkImage, VkDeviceMemory> CreateImageAndMemory(VulkanContext const &context, VkDevice device, VkExtent2D extent, VkFormat format,
                                                         bool isProtected) {
    bool const isDepth = VK_UTILS::IsVkDepthFormat(format);
    // blit() 需对任意纹理成立，故交换链图像同时具备 blit 源与目标用途（见 copyFrame / readPixels）
    constexpr VkImageUsageFlags kBlittable = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkImageCreateInfo const imageInfo = {
        .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags       = isProtected ? VK_IMAGE_CREATE_PROTECTED_BIT : VkImageCreateFlags(0),
        .imageType   = VK_IMAGE_TYPE_2D,
        .format      = format,
        .extent      = { extent.width, extent.height, 1 },
        .mipLevels   = 1,
        .arrayLayers = 1,
        .samples     = VK_SAMPLE_COUNT_1_BIT,
        .tiling      = VK_IMAGE_TILING_OPTIMAL,
        .usage       = kBlittable | (isDepth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),
    };
    VkImage  image  = VK_NULL_HANDLE;
    VkResult result = vkCreateImage(device, &imageInfo, kVkAlloc, &image);
    if (result != VK_SUCCESS) {
        LOG_CRITICAL("vkCreateImage failed. error={}", static_cast<int32_t>(result));
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, image, &memReqs);

    VkFlags const requiredMemoryFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | (isProtected ? VK_MEMORY_PROPERTY_PROTECTED_BIT : 0U);
    uint32_t const memoryTypeIndex    = context.SelectMemoryType(memReqs.memoryTypeBits, requiredMemoryFlags);
    if (memoryTypeIndex >= VK_MAX_MEMORY_TYPES) {
        LOG_CRITICAL("Unable to find a memory type meeting the requirements of swapchain image");
    }

    VkMemoryAllocateInfo const allocInfo = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = memReqs.size,
        .memoryTypeIndex = memoryTypeIndex,
    };
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    result                     = vkAllocateMemory(device, &allocInfo, kVkAlloc, &imageMemory);
    if (result != VK_SUCCESS) {
        LOG_CRITICAL("vkAllocateMemory failed. error={}", static_cast<int32_t>(result));
    }
    result = vkBindImageMemory(device, image, imageMemory, 0);
    if (result != VK_SUCCESS) {
        LOG_CRITICAL("vkBindImageMemory failed. error={}", static_cast<int32_t>(result));
    }
    return std::tuple(image, imageMemory);
}

VkFormat SelectDepthFormat(VkFormatList const &depthFormats, bool hasStencil) {
    auto const formatItr = std::find_if(depthFormats.begin(), depthFormats.end(), hasStencil ? VK_UTILS::IsVkStencilFormat : VK_UTILS::IsVkDepthFormat);
    if (formatItr == depthFormats.end()) {
        LOG_CRITICAL("Cannot find suitable swapchain depth format");
    }
    return *formatItr;
}

bool IsEquivalent(VkExtent2D const &a, VkExtent2D const &b) {
    return a.width == b.width && a.height == b.height;
}

}  // namespace

VulkanPlatformSwapChainBase::VulkanPlatformSwapChainBase(VulkanContext const &context, VkDevice device, VkQueue queue)
    : m_context(context)
    , m_device(device)
    , m_queue(queue) {}

VulkanPlatformSwapChainBase::~VulkanPlatformSwapChainBase() = default;

VulkanPlatform::SwapChainBundle VulkanPlatformSwapChainBase::GetSwapChainBundle() const {
    return m_swapChainBundle;
}

bool VulkanPlatformSwapChainBase::QueryCompositorTiming(Platform::CompositorTiming *outCompositorTiming) const {
    return false;
}

bool VulkanPlatformSwapChainBase::SetPresentFrameId(uint64_t frameId) const {
    return false;
}

bool VulkanPlatformSwapChainBase::QueryFrameTimestamps(uint64_t frameId, Platform::FrameTimestamps *outFrameTimestamps) const {
    return false;
}

VkImage VulkanPlatformSwapChainBase::CreateImage(VkExtent2D extent, VkFormat format, bool isProtected) {
    auto [image, memory] = CreateImageAndMemory(m_context, m_device, extent, format, isProtected);
    m_memory.insert({ image, memory });
    return image;
}

void VulkanPlatformSwapChainBase::Destroy() {
    if (m_swapChainBundle.depth != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_swapChainBundle.depth, kVkAlloc);
        if (auto const itr = m_memory.find(m_swapChainBundle.depth); itr != m_memory.end()) {
            vkFreeMemory(m_device, itr->second, kVkAlloc);
            m_memory.erase(itr);
        }
    }
    m_swapChainBundle.depth = VK_NULL_HANDLE;

    // surface 交换链的图像由呈现引擎持有，不归本类所有
    m_swapChainBundle.colors.clear();
}

VulkanPlatformSurfaceSwapChain::VulkanPlatformSurfaceSwapChain(VulkanContext const &context, VkPhysicalDevice physicalDevice, VkDevice device,
                                                              VkQueue queue, VkInstance instance, VkSurfaceKHR surface,
                                                              VkExtent2D fallbackExtent, uint64_t flags)
    : VulkanPlatformSwapChainBase(context, device, queue)
    , m_instance(instance)
    , m_physicalDevice(physicalDevice)
    , m_surface(surface)
    , m_fallbackExtent(fallbackExtent)
    , m_usesRGB((flags & kSwapChainConfigSRGBColorspace) != 0)
    , m_hasStencil((flags & kSwapChainConfigHasStencilBuffer) != 0)
    , m_isProtected((flags & kSwapChainConfigProtectedContent) != 0) {
    LOG_ASSERT(surface != VK_NULL_HANDLE);
    Create();
}

VulkanPlatformSurfaceSwapChain::~VulkanPlatformSurfaceSwapChain() {
    VulkanPlatformSurfaceSwapChain::Destroy();
    vkDestroySurfaceKHR(m_instance, m_surface, kVkAlloc);
}

VkResult VulkanPlatformSurfaceSwapChain::Create() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps);

    // 取比下限多一张：仅达到下限时可能需要等待呈现引擎释放上一帧的缓冲
    uint32_t const maxImageCount     = caps.maxImageCount;
    uint32_t       desiredImageCount = caps.minImageCount + 1;
    if (maxImageCount != 0 && desiredImageCount > maxImageCount) {
        LOG_WARN("Swapchain does not support {} images, falling back to minImageCount", desiredImageCount);
        desiredImageCount = caps.minImageCount;
    }

    std::array<VkFormat, 2> expectedFormats = {
        m_usesRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM,
        m_usesRGB ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM,
    };
    VkSurfaceFormatKHR surfaceFormat = {};
    std::vector<VkSurfaceFormatKHR> const surfaceFormats =
        VK_UTILS::enumerate(vkGetPhysicalDeviceSurfaceFormatsKHR, m_physicalDevice, m_surface);
    for (VkSurfaceFormatKHR const &format : surfaceFormats) {
        if (std::any_of(expectedFormats.begin(), expectedFormats.end(), [&format](VkFormat f) { return f == format.format; })) {
            surfaceFormat = format;
            break;
        }
    }
    if (surfaceFormat.format == VK_FORMAT_UNDEFINED) {
        LOG_CRITICAL("Cannot find suitable swapchain format");
    }

    // FIFO 在实践中普遍可用，仍显式校验以免触发校验层告警
    constexpr VkPresentModeKHR kDesiredPresentMode = VK_PRESENT_MODE_FIFO_KHR;
    std::vector<VkPresentModeKHR> const presentModes =
        VK_UTILS::enumerate(vkGetPhysicalDeviceSurfacePresentModesKHR, m_physicalDevice, m_surface);
    if (std::find(presentModes.begin(), presentModes.end(), kDesiredPresentMode) == presentModes.end()) {
        LOG_CRITICAL("Desired present mode is not supported by this device");
    }

    if (caps.currentExtent.width == kUndefinedVkExtent || caps.currentExtent.height == kUndefinedVkExtent) {
        m_swapChainBundle.extent = m_fallbackExtent;
    } else {
        m_swapChainBundle.extent = caps.currentExtent;
    }

    VkCompositeAlphaFlagBitsKHR const compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
                                                           ? VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
                                                           : VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    // preTransform 取 IDENTITY 表示旋转交由平台合成器处理，避免在着色器侧调整 MVP 与导数
    VkSwapchainCreateInfoKHR const createInfo = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .flags            = m_isProtected ? VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR : VkSwapchainCreateFlagsKHR(0),
        .surface          = m_surface,
        .minImageCount    = desiredImageCount,
        .imageFormat      = surfaceFormat.format,
        .imageColorSpace  = surfaceFormat.colorSpace,
        .imageExtent      = m_swapChainBundle.extent,
        .imageArrayLayers = 1,
        // blit 目标用于 copyFrame，blit 源用于 readPixels
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .preTransform     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha   = compositeAlpha,
        .presentMode      = kDesiredPresentMode,
        .clipped          = VK_TRUE,
        .oldSwapchain     = m_swapchain,
    };
    VkResult result = vkCreateSwapchainKHR(m_device, &createInfo, kVkAlloc, &m_swapchain);
    if (result != VK_SUCCESS) {
        LOG_CRITICAL("vkCreateSwapchainKHR failed. error={}", static_cast<int32_t>(result));
    }

    m_swapChainBundle.colors      = VK_UTILS::enumerate(vkGetSwapchainImagesKHR, m_device, m_swapchain);
    m_swapChainBundle.colorFormat = surfaceFormat.format;
    m_swapChainBundle.depthFormat = SelectDepthFormat(m_context.GetAttachmentDepthStencilFormats(), m_hasStencil);
    m_swapChainBundle.depth       = CreateImage(m_swapChainBundle.extent, m_swapChainBundle.depthFormat, m_isProtected);
    m_swapChainBundle.isProtected = m_isProtected;

    LOG_INFO("vkCreateSwapchainKHR: {}x{}, format={}, imageCount={}, depthFormat={}, protected={}", m_swapChainBundle.extent.width,
             m_swapChainBundle.extent.height, static_cast<int>(surfaceFormat.format), m_swapChainBundle.colors.size(),
             static_cast<int>(m_swapChainBundle.depthFormat), m_swapChainBundle.isProtected);

    VkSemaphoreCreateInfo const semaphoreCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    for (uint32_t i = 0; i < kImageReadySemaphoreCount; ++i) {
        result = vkCreateSemaphore(m_device, &semaphoreCreateInfo, kVkAlloc, m_imageReady + i);
        if (result != VK_SUCCESS) {
            LOG_CRITICAL("vkCreateSemaphore failed. error={}", static_cast<int32_t>(result));
        }
    }
    return result;
}

VkResult VulkanPlatformSurfaceSwapChain::Acquire(VulkanPlatform::ImageSyncData *outImageSyncData) {
    m_currentImageReadyIndex              = (m_currentImageReadyIndex + 1) % kImageReadySemaphoreCount;
    outImageSyncData->imageReadySemaphore = m_imageReady[m_currentImageReadyIndex];
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, outImageSyncData->imageReadySemaphore, VK_NULL_HANDLE,
                                            &outImageSyncData->imageIndex);

    // 不宜反复刷日志或反复重建，故只提示一次
    if (result == VK_SUBOPTIMAL_KHR && !m_suboptimal) {
        LOG_WARN("Vulkan: Suboptimal swap chain");
        m_suboptimal = true;
    }
    return result;
}

VkResult VulkanPlatformSurfaceSwapChain::Present(uint32_t index, VkSemaphore finished) {
    VkPresentInfoKHR const presentInfo = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &finished,
        .swapchainCount     = 1,
        .pSwapchains        = &m_swapchain,
        .pImageIndices      = &index,
    };
    VkResult result = vkQueuePresentKHR(m_queue, &presentInfo);
    if (result == VK_SUBOPTIMAL_KHR && !m_suboptimal) {
        LOG_WARN("Vulkan: Suboptimal swap chain");
        m_suboptimal = true;
    }
    return result;
}

bool VulkanPlatformSurfaceSwapChain::HasResized() const {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps);
    VkExtent2D perceivedExtent = caps.currentExtent;
    if (perceivedExtent.width == kUndefinedVkExtent || perceivedExtent.height == kUndefinedVkExtent) {
        perceivedExtent = m_fallbackExtent;
    }
    return !IsEquivalent(m_swapChainBundle.extent, perceivedExtent);
}

bool VulkanPlatformSurfaceSwapChain::IsProtected() const {
    return m_isProtected;
}

bool VulkanPlatformSurfaceSwapChain::QueryCompositorTiming(Platform::CompositorTiming *outCompositorTiming) const {
    return VulkanPlatformSwapChainBase::QueryCompositorTiming(outCompositorTiming);
}

bool VulkanPlatformSurfaceSwapChain::SetPresentFrameId(uint64_t frameId) const {
    return VulkanPlatformSwapChainBase::SetPresentFrameId(frameId);
}

bool VulkanPlatformSurfaceSwapChain::QueryFrameTimestamps(uint64_t frameId, Platform::FrameTimestamps *outFrameTimestamps) const {
    return VulkanPlatformSwapChainBase::QueryFrameTimestamps(frameId, outFrameTimestamps);
}

VkResult VulkanPlatformSurfaceSwapChain::Recreate() {
    Destroy();
    return Create();
}

void VulkanPlatformSurfaceSwapChain::Destroy() {
    // Vulkan 未定义交换链何时可安全销毁（KhronosGroup/Vulkan-Docs#1678），
    // 这里直接等待队列空闲；该路径只在尺寸变化等低频时机发生
    vkQueueWaitIdle(m_queue);

    VulkanPlatformSwapChainBase::Destroy();

    for (uint32_t i = 0; i < kImageReadySemaphoreCount; ++i) {
        if (m_imageReady[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, m_imageReady[i], kVkAlloc);
            m_imageReady[i] = VK_NULL_HANDLE;
        }
    }
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, kVkAlloc);
        m_swapchain = VK_NULL_HANDLE;
    }
}

VulkanPlatformHeadlessSwapChain::VulkanPlatformHeadlessSwapChain(VulkanContext const &context, VkDevice device, VkQueue queue, VkExtent2D extent,
                                                                uint64_t flags)
    : VulkanPlatformSwapChainBase(context, device, queue) {
    m_swapChainBundle.extent      = extent;
    m_swapChainBundle.colorFormat = (flags & kSwapChainConfigSRGBColorspace) != 0 ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

    auto &images = m_swapChainBundle.colors;
    images.reserve(kHeadlessSwapChainSize);
    for (uint32_t i = 0; i < kHeadlessSwapChainSize; ++i) {
        images.push_back(CreateImage(extent, m_swapChainBundle.colorFormat, false));
    }

    bool const hasStencil         = (flags & kSwapChainConfigHasStencilBuffer) != 0;
    m_swapChainBundle.depthFormat = SelectDepthFormat(m_context.GetAttachmentDepthStencilFormats(), hasStencil);
    m_swapChainBundle.depth       = CreateImage(extent, m_swapChainBundle.depthFormat, false);
}

VulkanPlatformHeadlessSwapChain::~VulkanPlatformHeadlessSwapChain() {
    VulkanPlatformHeadlessSwapChain::Destroy();
}

VkResult VulkanPlatformHeadlessSwapChain::Acquire(VulkanPlatform::ImageSyncData *outImageSyncData) {
    outImageSyncData->imageIndex = m_currentIndex;
    m_currentIndex               = (m_currentIndex + 1) % kHeadlessSwapChainSize;
    return VK_SUCCESS;
}

VkResult VulkanPlatformHeadlessSwapChain::Present(uint32_t index, VkSemaphore finished) {
    // headless 无呈现目标
    return VK_SUCCESS;
}

VkResult VulkanPlatformHeadlessSwapChain::Recreate() {
    LOG_CRITICAL("Recreate() is not supported by headless swapchain");
    return VK_ERROR_UNKNOWN;
}

bool VulkanPlatformHeadlessSwapChain::HasResized() const {
    return false;
}

bool VulkanPlatformHeadlessSwapChain::IsProtected() const {
    return false;
}

void VulkanPlatformHeadlessSwapChain::Destroy() {
    // 只在析构时调用：headless 不会重建
    for (VkImage image : m_swapChainBundle.colors) {
        vkDestroyImage(m_device, image, kVkAlloc);
        if (auto const itr = m_memory.find(image); itr != m_memory.end()) {
            vkFreeMemory(m_device, itr->second, kVkAlloc);
            m_memory.erase(itr);
        }
    }
    m_swapChainBundle.colors.clear();

    // 深度图像的内存由基类释放，须在颜色图像之后
    VulkanPlatformSwapChainBase::Destroy();
}

END_NS_BACKEND
