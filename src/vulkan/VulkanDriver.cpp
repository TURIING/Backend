#include "VulkanDriver.h"

#include "Backend/platform/VulkanPlatform.h"
#include "command/CommandStreamDispatcher.h"

BEGIN_NS_BACKEND

DriverPtr VulkanDriver::Create(VulkanPlatform *platform, VulkanContext &context,
                               const DriverConfig &config) {
    // TODO: 完整 Driver 实现
    (void)platform;
    (void)context;
    (void)config;
    return DriverPtr(new VulkanDriver());
}

Dispatcher VulkanDriver::GetDispatcher() const noexcept {
    return ConcreteDispatcher<VulkanDriver>::Make();
}

void VulkanDriver::terminate() {
}

FenceHandle VulkanDriver::createFenceS() noexcept {
    return {};
}

END_NS_BACKEND