#include "Backend/PlatformFactory.h"

#include "Backend/platform/VulkanPlatformApple.h"

BEGIN_NS_BACKEND

PlatformPtr PlatformFactory::Create(BackendType type) {
    switch (type) {
        case BackendType::VULKAN:
#if defined(BACKEND_SUPPORT_VULKAN)
#if PLATFORM_APPLE
            return PlatformPtr(new VulkanPlatformApple());
#endif
#else
            return nullptr;
#endif
        case BackendType::OPENGL:
            // TODO: OpenGL 平台尚未实现
            return nullptr;
        case BackendType::AUTO:
#if defined(BACKEND_SUPPORT_VULKAN)
#if PLATFORM_APPLE
            return PlatformPtr(new VulkanPlatformApple());
#endif
#else
            return nullptr;
#endif
        default:
            return nullptr;
    }
}

void PlatformFactory::Destroy(PlatformPtr platform) { platform.Reset(); }

END_NS_BACKEND