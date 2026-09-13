#pragma once

#include "Backend/DriverDefine.h"

#include "Utils/Utils.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>

#include "vk_mem_alloc.h"
#include "vulkan/resource/Resource.h"
#include "vulkan/resource/ResourceManager.h"

BEGIN_NS_BACKEND

DECLARE_CLASS_AND_UNIQUE_PTR(VulkanStageBuffer);

// 共享 CPU-GPU 暂存缓冲，可按需切分为多个 Segment 复用同一块显存
class VulkanStageBuffer : public NS_UTILS::Ref {
public:
    VulkanStageBuffer(VmaAllocator allocator, VmaAllocation memory, VkBuffer vkbuffer, uint32_t capacity, void* mapping)
        : m_allocator(allocator), m_memory(memory), m_vkbuffer(vkbuffer), m_capacity(capacity), m_mapping(mapping) {}

    ~VulkanStageBuffer() override;

    class Segment : public Resource {
    public:
        using OnRecycle = std::function<void(uint32_t offset)>;

        Segment(VulkanStageBuffer* parentStage, uint32_t capacity, uint32_t offset, OnRecycle&& onRecycleFn)
            : m_parentStage(parentStage), m_capacity(capacity), m_offset(offset), m_onRecycleFn(std::move(onRecycleFn)) {}

        ~Segment() override {
            if (m_onRecycleFn) {
                m_onRecycleFn(m_offset);
            }
        }

        NODISCARD VulkanStageBuffer* GetParentStage() const { return m_parentStage; }
        NODISCARD VkBuffer GetVkBuffer() const { return m_parentStage->GetVkBuffer(); }
        NODISCARD VmaAllocation GetMemory() const { return m_parentStage->GetMemory(); }
        NODISCARD uint32_t GetCapacity() const { return m_capacity; }
        NODISCARD uint32_t GetOffset() const { return m_offset; }

        NODISCARD void* GetMapping() const { return static_cast<char*>(m_parentStage->GetMapping()) + m_offset; }

    private:
        VulkanStageBuffer* const m_parentStage;
        const uint32_t           m_capacity;
        const uint32_t           m_offset;
        OnRecycle                m_onRecycleFn;
    };
    DECLARE_SHARE_PTR_CLASS(Segment);

    NODISCARD VmaAllocation GetMemory() const { return m_memory; }
    NODISCARD VkBuffer GetVkBuffer() const { return m_vkbuffer; }
    NODISCARD uint32_t GetCapacity() const { return m_capacity; }
    NODISCARD void* GetMapping() const { return m_mapping; }
    NODISCARD uint32_t GetCurrentOffset() const { return m_currentOffset; }

    NODISCARD bool IsSafeToReset() const { return m_segments.empty(); }

    void Reset() { m_currentOffset = 0; }

    // segmentOffset 须不小于当前 currentOffset，numBytes 须已按 nonCoherentAtomSize 对齐
    NODISCARD SegmentPtr AcquireSegment(const ResourceManagerPtr& resourceManager, uint32_t segmentOffset, uint32_t numBytes);

private:
    const VmaAllocator  m_allocator;
    const VmaAllocation m_memory;
    const VkBuffer      m_vkbuffer;
    const uint32_t      m_capacity;

    void* m_mapping;

    uint32_t m_currentOffset = 0;

    // 切分起点 → Segment；Segment 析构时经 OnRecycle 回调摘除自身条目
    std::unordered_map<uint32_t, Segment*> m_segments;
};

template <>
ResourceType Resource::GetTypeEnum<VulkanStageBuffer::Segment>() const noexcept;

END_NS_BACKEND
