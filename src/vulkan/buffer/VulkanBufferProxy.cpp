#include "vulkan/buffer/VulkanBufferProxy.h"

#include "Utils/Log.h"

#include <cstring>

#include "vulkan/VkDef.h"
#include "vulkan/commands/VulkanCommandBuffer.h"
#include "vulkan/stage/VulkanStageBuffer.h"

BEGIN_NS_BACKEND

VulkanBufferProxy::VulkanBufferProxy(const VulkanContextPtr& context, VmaAllocator allocator, const VulkanStagePoolPtr& stagePool,
                                     const VulkanBufferCachePtr& bufferCache, VulkanBufferBinding binding, BufferUsage usage,
                                     uint32_t numBytes)
    : m_stagingBufferBypassEnabled(context->IsStagingBufferBypassEnabled()),
      m_allocator(allocator),
      m_stagePool(stagePool),
      m_bufferCache(bufferCache),
      m_buffer(m_bufferCache->Acquire(binding, numBytes)),
      m_usage(usage) {}

void VulkanBufferProxy::LoadFromCpu(VulkanCommandBuffer& commands, void const* cpuData, uint32_t byteOffset, uint32_t numBytes) {
    VulkanGpuBuffer const* gpuBuffer = m_buffer->GetGpuBuffer();

    // 计数 1 表示除本代理外无借用者，缓冲当前未被 GPU 使用
    bool const isAvailable   = m_buffer->GetRefCount() == 1;
    bool const isMemcopyable = gpuBuffer->allocationInfo.pMappedData != nullptr;

    // 内容标为 STATIC/SHARED_WRITE 时，映射内存可安全直写
    bool const isStaticOrShared = HasAnyFlag(m_usage, BufferUsage::STATIC | BufferUsage::SHARED_WRITE_BIT);
    bool const useMemcpy        = ((isAvailable && m_stagingBufferBypassEnabled) || isStaticOrShared) && isMemcopyable;

    // 借用登记先于任何写入：提交完成前该缓冲不得被回收
    commands.Acquire(m_buffer);

    if (useMemcpy) {
        char* dest = static_cast<char*>(gpuBuffer->allocationInfo.pMappedData) + byteOffset;
        memcpy(dest, cpuData, numBytes);
        vmaFlushAllocation(m_allocator, gpuBuffer->vmaAllocation, byteOffset, numBytes);
        return;
    }

    VulkanStageBuffer::SegmentPtr stage = m_stagePool->AcquireStage(numBytes);
    LOG_ASSERT(stage->GetMemory() != VK_NULL_HANDLE);

    commands.Acquire(stage);
    memcpy(stage->GetMapping(), cpuData, numBytes);
    vmaFlushAllocation(m_allocator, stage->GetMemory(), stage->GetOffset(), numBytes);

    // 同一命令缓冲内上次读之后又写，需用屏障隔开读与写
    if (commands.Age() == m_lastReadAge) {
        VkAccessFlags        srcAccess = 0;
        VkPipelineStageFlags srcStage  = 0;

        switch (GetBinding()) {
            case VulkanBufferBinding::Vertex:
                srcAccess = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
                srcStage  = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
                break;
            case VulkanBufferBinding::Index:
                srcAccess = VK_ACCESS_INDEX_READ_BIT;
                srcStage  = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
                break;
            case VulkanBufferBinding::Uniform:
                srcAccess = VK_ACCESS_SHADER_READ_BIT;
                srcStage  = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                break;
            case VulkanBufferBinding::ShaderStorage:
            case VulkanBufferBinding::Unknown:
                break;
        }

        VkBufferMemoryBarrier const readToWriteBarrier{
            .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask       = srcAccess,
            .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer              = GetVkBuffer(),
            .offset              = byteOffset,
            .size                = numBytes,
        };
        vkCmdPipelineBarrier(commands.Buffer(), srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &readToWriteBarrier, 0, nullptr);
    }

    VkBufferCopy const region{
        .srcOffset = stage->GetOffset(),
        .dstOffset = byteOffset,
        .size      = numBytes,
    };
    vkCmdCopyBuffer(commands.Buffer(), stage->GetVkBuffer(), GetVkBuffer(), 1, &region);

    // 拷贝须在后续绘制前完成；dstStage 含 TRANSFER 以挡住同缓冲的下一次上传
    VkAccessFlags        dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    VkPipelineStageFlags dstStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT;

    switch (GetBinding()) {
        case VulkanBufferBinding::Vertex:
            dstAccessMask |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            dstStageMask |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
            break;
        case VulkanBufferBinding::Index:
            dstAccessMask |= VK_ACCESS_INDEX_READ_BIT;
            dstStageMask |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
            break;
        case VulkanBufferBinding::Uniform:
            dstAccessMask |= VK_ACCESS_UNIFORM_READ_BIT;
            dstStageMask |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
            break;
        case VulkanBufferBinding::ShaderStorage:
        case VulkanBufferBinding::Unknown:
            break;
    }

    VkBufferMemoryBarrier const writeToReadBarrier{
        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask       = dstAccessMask,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer              = GetVkBuffer(),
        .offset              = byteOffset,
        .size                = numBytes,
    };
    vkCmdPipelineBarrier(commands.Buffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, dstStageMask, 0, 0, nullptr, 1, &writeToReadBarrier, 0, nullptr);
}

VkBuffer VulkanBufferProxy::GetVkBuffer() const noexcept { return m_buffer->GetGpuBuffer()->vkbuffer; }

VulkanBufferBinding VulkanBufferProxy::GetBinding() const noexcept { return m_buffer->GetGpuBuffer()->binding; }

void VulkanBufferProxy::ReferencedBy(VulkanCommandBuffer& commands) {
    commands.Acquire(m_buffer);
    m_lastReadAge = commands.Age();
}

END_NS_BACKEND
