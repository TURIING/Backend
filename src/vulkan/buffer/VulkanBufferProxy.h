#pragma once

#include "Backend/DriverDefine.h"

#include "Utils/Utils.h"

#include <cstdint>

#include "vulkan/VulkanContext.h"
#include "vulkan/buffer/VulkanBufferCache.h"

BEGIN_NS_BACKEND

// 对 VulkanBuffer 的动态包装：运行期可替换其引用的缓冲，不影响外部对象
class VulkanBufferProxy {
public:
    VulkanBufferProxy(const VulkanContextPtr& context, VmaAllocator allocator, const VulkanBufferCachePtr& bufferCache, VulkanBufferBinding binding,
                      BufferUsage usage, uint32_t numBytes);

    VkBuffer GetVkBuffer() const noexcept;

private:
    VulkanBufferBinding GetBinding() const noexcept;

    bool const           m_stagingBufferBypassEnabled;
    VmaAllocator         m_allocator;
    VulkanBufferCachePtr m_bufferCache;

    VulkanBufferPtr m_buffer;
    BufferUsage     m_usage;
};

END_NS_BACKEND
