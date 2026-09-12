#include "VulkanDriver.h"

#include "Backend/platform/VulkanPlatform.h"

#include "Utils/Log.h"
#include "Utils/Macro.h"

#include "HwDefine.h"
#include "command/CommandStreamDispatcher.h"
#include "vulkan/VkDef.h"
#include "vulkan/VulkanHandle.h"
#include "vulkan/buffer/VulkanBuffer.h"
#include "vulkan/resource/ResourceManager.h"

BEGIN_NS_BACKEND

namespace {

VmaAllocator CreateAllocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device) noexcept {
    VmaVulkanFunctions const vulkanFunctions{
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr   = vkGetDeviceProcAddr,
    };

    VmaAllocatorCreateInfo const allocatorInfo{
        // 后端单线程访问 VMA，关掉其内部同步以省开销
        .flags            = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT,
        .physicalDevice   = physicalDevice,
        .device           = device,
        .pVulkanFunctions = &vulkanFunctions,
        .instance         = instance,
    };

    VmaAllocator allocator = VK_NULL_HANDLE;
    if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS) {
        LOG_CRITICAL("VulkanDriver - failed to create the VMA allocator");
    }
    return allocator;
}

}  // namespace

VulkanDriver::VulkanDriver(const VulkanPlatformPtr &platform, const VulkanContextPtr &context, const DriverConfig &config)
    : m_resMgr(new ResourceManager(config.handleArenaSize, false, false)),
      m_context(context),
      m_allocator(CreateAllocator(platform->GetVkInstance(), platform->GetVkPhysicalDevice(), platform->GetVkDevice())),
      m_bufferCache(new VulkanBufferCache(m_context, m_resMgr, m_allocator)),
      m_stagePool(new VulkanStagePool(m_context, m_resMgr, m_allocator)) {}

VulkanDriver::~VulkanDriver() noexcept { DestroyResources(); }

DriverPtr VulkanDriver::Create(VulkanPlatform *platform, const VulkanContextPtr &context, const DriverConfig &config) {
    return DriverPtr(new VulkanDriver(VulkanPlatformPtr(platform), context, config));
}

Dispatcher VulkanDriver::GetDispatcher() const noexcept { return ConcreteDispatcher<VulkanDriver>::Make(); }

void VulkanDriver::DestroyResources() noexcept {
    m_resMgr->Terminate();

    m_bufferCache.Reset();
    m_stagePool.Reset();

    if (m_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }
}

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

Handle<HwVertexBuffer> VulkanDriver::CreateVertexBufferS() noexcept { return m_resMgr->AllocHandle<VulkanVertexBuffer>(); }
void VulkanDriver::CreateVertexBufferR(Handle<HwVertexBuffer> vbh, uint32_t vertexCount, Handle<HwVertexBufferInfo> vbih,
                                       NS_UTILS::ImmutableString &&tag) {
    auto vbi = m_resMgr->Acquire<VulkanVertexBufferInfo>(vbih);
    auto vb  = m_resMgr->Make<VulkanVertexBuffer>(vbh, m_context, m_bufferCache, vertexCount, vbi);
    m_resMgr->AssociateTagToHandle(vbh.GetId(), std::move(tag));
}

Handle<HwBufferObject> VulkanDriver::CreateBufferObjectS() noexcept { return m_resMgr->AllocHandle<VulkanBufferObject>(); }
void VulkanDriver::CreateBufferObjectR(Handle<HwBufferObject> boh, uint32_t byteCount, BufferObjectBinding bindingType, BufferUsage usage,
                                       NS_UTILS::ImmutableString &&tag) {
    auto bo = m_resMgr->Make<VulkanBufferObject>(boh, m_context, m_allocator, m_bufferCache, byteCount, bindingType, usage);
    m_resMgr->AssociateTagToHandle(boh.GetId(), std::move(tag));
}

void VulkanDriver::DestroyBufferObject(BufferObjectHandle boh) {
    if (!boh) {
        return;
    }
    auto bo = m_resMgr->Acquire<VulkanBufferObject>(boh);
    m_resMgr->Destroy(bo);
}

void VulkanDriver::SetVertexBufferObject(VertexBufferHandle vbh, uint32_t index, BufferObjectHandle boh) {
    auto vb = m_resMgr->Acquire<VulkanVertexBuffer>(vbh);
    auto bo = m_resMgr->Acquire<VulkanBufferObject>(boh);

    LOG_ASSERT(bo->bindingType == BufferObjectBinding::Vertex);
    vb->SetBuffer(bo, index);
}

void VulkanDriver::createFenceR(FenceHandle, utils::ImmutableString &&tag) {}

void VulkanDriver::destroyFence(FenceHandle fh) {}

void VulkanDriver::terminate() { DestroyResources(); }

END_NS_BACKEND
