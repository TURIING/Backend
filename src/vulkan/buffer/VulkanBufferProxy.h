#pragma once

#include "Backend/DriverDefine.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/buffer/VulkanBufferCache.h"

#include "Utils/mem/SharedPtr.h"

#include <cstdint>

BEGIN_NS_BACKEND

// 对 VulkanBuffer 的动态包装：运行期可替换其引用的缓冲，不影响外部对象
class VulkanBufferProxy {
public:
    VulkanBufferProxy(VulkanContext const& context, VmaAllocator allocator,
                      VulkanBufferCache& bufferCache, VulkanBufferBinding binding,
                      BufferUsage usage, uint32_t numBytes);

    VkBuffer GetVkBuffer() const noexcept;

private:
    VulkanBufferBinding GetBinding() const noexcept;

    bool const         m_stagingBufferBypassEnabled;
    VmaAllocator       m_allocator;
    VulkanBufferCache& m_bufferCache;

    NS_UTILS::SharedPtr<VulkanBuffer> m_buffer;
    BufferUsage                       m_usage;
};

END_NS_BACKEND
