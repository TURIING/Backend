#include "VulkanDriver.h"

#include "Backend/platform/VulkanPlatform.h"

#include "Utils/Log.h"
#include "Utils/Macro.h"

#include "HwDefine.h"
#include "command/CommandStreamDispatcher.h"
#include "vulkan/VulkanHandle.h"
#include "vulkan/resource/ResourceManager.h"

BEGIN_NS_BACKEND
VulkanDriver::VulkanDriver(const VulkanPlatformPtr &platform, const VulkanContextPtr &context, const DriverConfig &config)
    : m_resMgr(new ResourceManager(config.handleArenaSize, false, false)) {}

DriverPtr VulkanDriver::Create(VulkanPlatform *platform, VulkanContext &context, const DriverConfig &config) {
    // TODO: 完整 Driver 实现
    (void)platform;
    (void)context;
    (void)config;
    return DriverPtr(new VulkanDriver(platform, context, config));
}

Dispatcher VulkanDriver::GetDispatcher() const noexcept { return ConcreteDispatcher<VulkanDriver>::Make(); }

void VulkanDriver::tick(int dummy) {}

void VulkanDriver::beginFrame(int64_t monotonic_clock_ns, int64_t refreshIntervalNs, uint32_t frameId) { LOG_INFO("VulkanDriver beginFrame"); }

void VulkanDriver::flush(int dummy) {}

void VulkanDriver::finish(int dummy) {}

void VulkanDriver::resetState(int dummy) {}

FenceHandle VulkanDriver::createFenceS() noexcept {
    static uint32_t sNextFenceId = 0;
    return FenceHandle(sNextFenceId++);
}

Handle<HwVertexBufferInfo> VulkanDriver::CreateVertexBufferInfoS() noexcept { return m_resMgr->AllocHandle<VulkanVertexBufferInfo>(); }
void VulkanDriver::CreateVertexBufferInfoR(Handle<HwVertexBufferInfo> vbInfoHandle, uint8_t bufferCount, uint8_t attributeCount,
                                           AttributeArray attributes, NS_UTILS::ImmutableString &&tag) {
    auto vbInfo = m_resMgr->Make<VulkanVertexBufferInfo>(vbInfoHandle, bufferCount, attributeCount, attributes);
    m_resMgr->AssociateTagToHandle(vbInfoHandle.GetId(), std::move(tag));
}

void VulkanDriver::createFenceR(FenceHandle, utils::ImmutableString &&tag) {}

void VulkanDriver::destroyFence(FenceHandle fh) {}

void VulkanDriver::terminate() {}

END_NS_BACKEND
