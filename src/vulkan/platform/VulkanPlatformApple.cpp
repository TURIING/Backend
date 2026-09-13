#include "Backend/platform/VulkanPlatformApple.h"

#include <tuple>

#include "vulkan/VkDef.h"
#include "vulkan/VkUtils.h"

BEGIN_NS_BACKEND

VulkanPlatform::ExtensionSet VulkanPlatformApple::GetSwapchainInstanceExtensions() const {
    return { VK_EXT_METAL_SURFACE_EXTENSION_NAME };
}

VulkanPlatform::SurfaceBundle VulkanPlatformApple::CreateVkSurfaceKHR(void *nativeWindow, VkInstance instance,
                                                                     uint64_t flags) const noexcept {
    // headless 无原生窗口，尺寸由调用方以 extent 指定
    if (nativeWindow == nullptr) {
        return std::make_tuple(VK_NULL_HANDLE, VkExtent2D{});
    }

    CAMetalLayer *mlayer = static_cast<CAMetalLayer *>(nativeWindow);
    if (vkCreateMetalSurfaceEXT == nullptr) {
        LOG_CRITICAL("Unable to load vkCreateMetalSurfaceEXT");
    }

    VkMetalSurfaceCreateInfoEXT createInfo = {
        .sType  = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
        .pLayer = mlayer,
    };

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    CALL_VK(vkCreateMetalSurfaceEXT(instance, &createInfo, nullptr, &surface));
    if (surface == VK_NULL_HANDLE) {
        return std::make_tuple(VK_NULL_HANDLE, VkExtent2D{});
    }

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(GetVkPhysicalDevice(), surface, &caps);
    // currentExtent 未定义时尺寸由 surface 自身决定，取 minImageExtent 兜底
    VkExtent2D const extent = (caps.currentExtent.width == kUndefinedVkExtent || caps.currentExtent.height == kUndefinedVkExtent)
                                  ? caps.minImageExtent
                                  : caps.currentExtent;

    return std::make_tuple(surface, extent);
}

END_NS_BACKEND
