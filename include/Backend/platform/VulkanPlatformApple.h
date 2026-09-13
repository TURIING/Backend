#pragma once

#include "../DriverDefine.h"
#include "VulkanPlatform.h"
BEGIN_NS_BACKEND

class VulkanPlatformApple : public VulkanPlatform {
public:
    ExtensionSet GetSwapchainInstanceExtensions() const override;
    SurfaceBundle CreateVkSurfaceKHR(void *nativeWindow, VkInstance instance, uint64_t flags) const noexcept override;
};
END_NS_BACKEND
