#include "vulkan/buffer/VulkanBufferProxy.h"

BEGIN_NS_BACKEND

VulkanBufferProxy::VulkanBufferProxy(VulkanContext const& context, VmaAllocator allocator,
                                     VulkanBufferCache& bufferCache, VulkanBufferBinding binding,
                                     BufferUsage usage, uint32_t numBytes)
    : m_stagingBufferBypassEnabled(context.IsStagingBufferBypassEnabled()),
      m_allocator(allocator),
      m_bufferCache(bufferCache),
      m_buffer(m_bufferCache.Acquire(binding, numBytes)),
      m_usage(usage) {}

VkBuffer VulkanBufferProxy::GetVkBuffer() const noexcept {
    return m_buffer->GetGpuBuffer()->vkbuffer;
}

VulkanBufferBinding VulkanBufferProxy::GetBinding() const noexcept {
    return m_buffer->GetGpuBuffer()->binding;
}

END_NS_BACKEND
