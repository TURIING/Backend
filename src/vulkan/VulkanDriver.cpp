#include "VulkanDriver.h"

#include "Backend/platform/VulkanPlatform.h"

#include "Utils/Log.h"

#include "command/CommandStreamDispatcher.h"

BEGIN_NS_BACKEND

DriverPtr VulkanDriver::Create(VulkanPlatform *platform, VulkanContext &context, const DriverConfig &config) {
    // TODO: 完整 Driver 实现
    (void)platform;
    (void)context;
    (void)config;
    return DriverPtr(new VulkanDriver());
}

Dispatcher VulkanDriver::GetDispatcher() const noexcept { return ConcreteDispatcher<VulkanDriver>::Make(); }

void VulkanDriver::tick(int dummy) {}

void VulkanDriver::beginFrame(int64_t monotonic_clock_ns, int64_t refreshIntervalNs, uint32_t frameId) {
    LOG_INFO("VulkanDriver beginFrame");
}

void VulkanDriver::flush(int dummy) {}

void VulkanDriver::finish(int dummy) {}

void VulkanDriver::resetState(int dummy) {}

FenceHandle VulkanDriver::createFenceS() noexcept {
    static uint32_t sNextFenceId = 0;
    return FenceHandle(sNextFenceId++);
}

void VulkanDriver::createFenceR(FenceHandle, utils::ImmutableString &&tag) {}

void VulkanDriver::destroyFence(FenceHandle fh) {}

void VulkanDriver::terminate() {}

END_NS_BACKEND
