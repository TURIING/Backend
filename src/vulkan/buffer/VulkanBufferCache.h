#pragma once

#include "Backend/DriverDefine.h"

#include "vulkan/VulkanContext.h"
#include "vulkan/buffer/VulkanBuffer.h"
#include "vulkan/resource/ResourceManager.h"

#include "Utils/mem/SharedPtr.h"

#include <cstdint>
#include <map>

BEGIN_NS_BACKEND

// OnRecycle 回调捕获本对象，缓存必须比其产出的所有 VulkanBuffer 存活更久
class VulkanBufferCache {
public:
    VulkanBufferCache(VulkanContext const& context, ResourceManager& resourceManager, VmaAllocator allocator);

    VulkanBufferCache(const VulkanBufferCache&)            = delete;
    VulkanBufferCache& operator=(const VulkanBufferCache&) = delete;

    NS_UTILS::SharedPtr<VulkanBuffer> Acquire(VulkanBufferBinding binding, uint32_t numBytes) noexcept;

    void Gc() noexcept;

    // 须在 VkDevice 仍存活时调用
    void Terminate() noexcept;

private:
    struct UnusedGpuBuffer {
        uint64_t               lastAccessed;
        VulkanGpuBuffer const* gpuBuffer;
    };

    using BufferPool = std::multimap<uint32_t, UnusedGpuBuffer>;

    void                   Release(VulkanGpuBuffer const* gpuBuffer) noexcept;
    VulkanGpuBuffer const* Allocate(VulkanBufferBinding binding, uint32_t numBytes) noexcept;
    void                   Destroy(VulkanGpuBuffer const* gpuBuffer) noexcept;
    BufferPool&            GetPool(VulkanBufferBinding binding) noexcept;

    VulkanContext const& m_context;
    ResourceManager&     m_resourceManager;
    VmaAllocator         m_allocator;

    // 4 种非 Unknown binding 各占一个池
    static constexpr int kMaxPoolCount = 4;
    BufferPool           m_gpuBufferPools[kMaxPoolCount];

    uint64_t m_currentFrame = 0;
};

END_NS_BACKEND
