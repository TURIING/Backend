#include "vulkan/stage/VulkanStageBuffer.h"

#include "Utils/Log.h"

#include "vulkan/VkDef.h"

BEGIN_NS_BACKEND

VulkanStageBuffer::~VulkanStageBuffer() {
    LOG_ASSERT(IsSafeToReset());

    vmaDestroyBuffer(m_allocator, m_vkbuffer, m_memory);

#if BVK_ENABLED(BVK_DEBUG_STAGING_ALLOCATION)
    LOG_DEBUG("VulkanStageBuffer - destroyed a staging buffer {} of size {}", m_vkbuffer, m_capacity);
#endif
}

VulkanStageBuffer::SegmentPtr VulkanStageBuffer::AcquireSegment(const ResourceManagerPtr& resourceManager, uint32_t segmentOffset,
                                                               uint32_t numBytes) {
    SegmentPtr segment =
        resourceManager->AllocateAndConstruct<Segment>(this, numBytes, segmentOffset, [this](uint32_t offset) { m_segments.erase(offset); });

    m_segments.insert({ segmentOffset, segment.Get() });
    m_currentOffset = segmentOffset + numBytes;

    return segment;
}

template <>
ResourceType Resource::GetTypeEnum<VulkanStageBuffer::Segment>() noexcept {
    return ResourceType::StageSegment;
}

END_NS_BACKEND
