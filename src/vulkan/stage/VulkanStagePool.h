#pragma once

#include "Utils/Utils.h"

#include <cstdint>
#include <map>

#include "vulkan/VulkanContext.h"
#include "vulkan/resource/ResourceManager.h"
#include "vulkan/stage/VulkanStageBuffer.h"

BEGIN_NS_BACKEND

// 暂存缓冲池：按容量复用缓冲，并周期性回收已无 Segment 占用的缓冲
DECLARE_CLASS_AND_SHARE_PTR(VulkanStagePool);

class VulkanStagePool : public NS_UTILS::Ref {
public:
    VulkanStagePool(const VulkanContextPtr& context, const ResourceManagerPtr& resourceManager, VmaAllocator allocator);

    VulkanStagePool(const VulkanStagePool&)            = delete;
    VulkanStagePool& operator=(const VulkanStagePool&) = delete;

    // 非线程安全，仅驱动线程调用；alignment 为 0 表示不对齐偏移
    NODISCARD VulkanStageBuffer::SegmentPtr AcquireStage(uint32_t numBytes, uint32_t alignment = 0) noexcept;

    void Gc() noexcept;

    // 须在 VkDevice 仍存活、且全部 Segment 已回收后调用
    void Terminate() noexcept;

private:
    NODISCARD uint32_t alignToNonCoherentAtomSize(uint32_t numBytes) const noexcept;

    NODISCARD VulkanStageBufferPtr allocateNewStage(uint32_t capacity) noexcept;

    void destroyStage(VulkanStageBufferPtr& stage) noexcept;

    VulkanContextPtr   m_context;
    ResourceManagerPtr m_resourceManager;
    VmaAllocator       m_allocator;

    // 剩余可切分空间 → 缓冲，lower_bound(numBytes) 命中容量足够的候选
    std::multimap<uint32_t, VulkanStageBufferPtr> m_stages;

    uint64_t m_currentFrame = 0;
};

END_NS_BACKEND
