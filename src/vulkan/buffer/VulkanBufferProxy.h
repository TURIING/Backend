#pragma once

#include "Backend/DriverDefine.h"

#include "Utils/Utils.h"

#include <cstdint>

#include "vulkan/VulkanContext.h"
#include "vulkan/buffer/VulkanBufferCache.h"
#include "vulkan/stage/VulkanStagePool.h"

BEGIN_NS_BACKEND

struct VulkanCommandBuffer;

// 对 VulkanBuffer 的动态包装：运行期可替换其引用的缓冲，不影响外部对象
class VulkanBufferProxy {
public:
    VulkanBufferProxy(const VulkanContextPtr& context, VmaAllocator allocator, const VulkanStagePoolPtr& stagePool,
                      const VulkanBufferCachePtr& bufferCache, VulkanBufferBinding binding, BufferUsage usage,
                      uint32_t numBytes);

    void LoadFromCpu(VulkanCommandBuffer& commands, void const* cpuData, uint32_t byteOffset, uint32_t numBytes);

    NODISCARD VkBuffer GetVkBuffer() const noexcept;

    void ReferencedBy(VulkanCommandBuffer& commands);

private:
    NODISCARD VulkanBufferBinding GetBinding() const noexcept;

    bool const           m_stagingBufferBypassEnabled;
    VmaAllocator         m_allocator;
    VulkanStagePoolPtr   m_stagePool;
    VulkanBufferCachePtr m_bufferCache;
    VulkanBufferPtr      m_buffer;
    uint32_t             m_lastReadAge = 0;
    BufferUsage          m_usage;
};

END_NS_BACKEND
