#include "Backend/platform/VulkanPlatformApple.h"

#include "vulkan/VkDef.h"
#include "vulkan/VkUtils.h"

BEGIN_NS_BACKEND

VulkanPlatform::ExtensionSet VulkanPlatformApple::getSwapchainInstanceExtensions() const {
    return { VK_EXT_METAL_SURFACE_EXTENSION_NAME };
}

VkSurfaceKHR VulkanPlatformApple::createVkSurfaceKHR(void *nativeWindow, VkInstance instance,
                                                     uint64_t flags) const noexcept {
    CAMetalLayer *mlayer = static_cast<CAMetalLayer *>(nativeWindow);
    if (mlayer == nullptr) {
        LOG_CRITICAL("Unable to obtain Metal-backed layer");
    }

    if (vkCreateMetalSurfaceEXT == nullptr) {
        LOG_CRITICAL("Unable to load vkCreateMetalSurfaceEXT");
    }

    VkMetalSurfaceCreateInfoEXT createInfo = {
        .sType  = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
        .pLayer = mlayer,
    };

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    CALL_VK(vkCreateMetalSurfaceEXT(instance, &createInfo, nullptr, &surface));
    return surface;
}

END_NS_BACKEND