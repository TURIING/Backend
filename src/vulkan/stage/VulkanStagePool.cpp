#include "vulkan/stage/VulkanStagePool.h"

#include "Utils/Log.h"

#include <algorithm>
#include <utility>

#include "vulkan/VkDef.h"

BEGIN_NS_BACKEND

namespace {

constexpr uint32_t kStageSize              = 1 << 20;
constexpr uint32_t kMaxEmptyStagesToRetain = 1;
constexpr uint32_t kTimeBeforeEviction     = 3;

// 不假定 alignment 为 2 的幂，故不用 ALIGN_UP
uint32_t alignValue(uint32_t value, uint32_t alignment) {
    if (alignment == 0) {
        return value;
    }

    uint32_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

}  // namespace

VulkanStagePool::VulkanStagePool(const VulkanContextPtr& context, const ResourceManagerPtr& resourceManager, VmaAllocator allocator)
    : m_context(context), m_resourceManager(resourceManager), m_allocator(allocator) {}

VulkanStageBuffer::SegmentPtr VulkanStagePool::AcquireStage(uint32_t numBytes, uint32_t alignment) noexcept {
    // 按原子大小上取整，保证 host flush 只覆盖本 Segment 涉及的原子
    numBytes = alignToNonCoherentAtomSize(numBytes);

    VulkanStageBufferPtr stage;
    uint32_t             segmentOffset = 0;

    auto iter = m_stages.lower_bound(numBytes);
    while (iter != m_stages.end()) {
        segmentOffset = alignValue(iter->second->GetCurrentOffset(), alignment);

        if (segmentOffset >= iter->second->GetCurrentOffset() && iter->second->GetCapacity() - numBytes >= segmentOffset) {
            stage = std::move(iter->second);
            m_stages.erase(iter);
            break;
        }

        ++iter;
    }

    if (!stage) {
        stage = allocateNewStage(std::max(numBytes, kStageSize));
    }

    auto segment = stage->AcquireSegment(m_resourceManager, segmentOffset, numBytes);

    uint32_t const spaceRemaining = stage->GetCapacity() - stage->GetCurrentOffset();
    m_stages.insert({ spaceRemaining, std::move(stage) });

    return segment;
}

void VulkanStagePool::Gc() noexcept {
    // 帧计数留给后续 image 淘汰路径；缓冲淘汰只判空，前几帧跳过以保持与上游一致的节奏
    if (++m_currentFrame <= kTimeBeforeEviction) {
        return;
    }

    decltype(m_stages) freeStages;
    freeStages.swap(m_stages);

    uint8_t freeStageCount = 0;  // 假定空缓冲不会超过 255 个
    for (auto& pair : freeStages) {
        if (!pair.second->IsSafeToReset()) {
            m_stages.insert(std::move(pair));
            continue;
        }

        if (++freeStageCount > kMaxEmptyStagesToRetain) {
            destroyStage(pair.second);
            continue;
        }

        pair.second->Reset();
        uint32_t const capacity = pair.second->GetCapacity();
        m_stages.insert({ capacity, std::move(pair.second) });
    }
}

void VulkanStagePool::Terminate() noexcept {
    for (auto& pair : m_stages) {
        destroyStage(pair.second);
    }
    m_stages.clear();
}

uint32_t VulkanStagePool::alignToNonCoherentAtomSize(uint32_t numBytes) const noexcept {
    return alignValue(numBytes, static_cast<uint32_t>(m_context->GetPhysicalDeviceLimits().nonCoherentAtomSize));
}

VulkanStageBufferPtr VulkanStagePool::allocateNewStage(uint32_t capacity) noexcept {
    VkBufferCreateInfo const bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = alignToNonCoherentAtomSize(capacity),
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };

    // 暂存缓冲需在 CPU 侧持久写入：显式声明 host 访问意图并预先映射，省去 map/unmap 往返
    VmaAllocationCreateInfo const allocInfo{
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VkBuffer          buffer = VK_NULL_HANDLE;
    VmaAllocation     memory = VK_NULL_HANDLE;
    VmaAllocationInfo allocationInfo{};
    VkResult const    result = vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &buffer, &memory, &allocationInfo);

#if BVK_ENABLED(BVK_DEBUG_STAGING_ALLOCATION)
    if (result != VK_SUCCESS) {
        LOG_ERROR("VulkanStagePool - failed to allocate a staging buffer of size {}, error: {}", capacity, result);
    } else {
        LOG_DEBUG("VulkanStagePool - allocated a staging buffer {} of size {}", buffer, capacity);
    }
#endif

    return NS_UTILS::MakeUnique<VulkanStageBuffer>(m_allocator, memory, buffer, capacity, allocationInfo.pMappedData);
}

void VulkanStagePool::destroyStage(VulkanStageBufferPtr& stage) noexcept { stage.Reset(); }

END_NS_BACKEND
