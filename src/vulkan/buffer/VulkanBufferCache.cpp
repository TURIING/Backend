#include "vulkan/buffer/VulkanBufferCache.h"

#include "vulkan/VkDef.h"

#include "Utils/Log.h"

#include <utility>

BEGIN_NS_BACKEND

namespace {

VkBufferUsageFlags getVkBufferUsage(VulkanBufferBinding usage) {
    switch (usage) {
        case VulkanBufferBinding::Vertex:
            return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        case VulkanBufferBinding::Index:
            return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        case VulkanBufferBinding::Uniform:
            return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        case VulkanBufferBinding::ShaderStorage:
            return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        case VulkanBufferBinding::Unknown:
            return 0;
    }
    return 0;
}

}  // namespace

VulkanBufferCache::VulkanBufferCache(VulkanContext const& context, ResourceManager& resourceManager,
                                     VmaAllocator allocator)
    : m_context(context), m_resourceManager(resourceManager), m_allocator(allocator) {}

NS_UTILS::SharedPtr<VulkanBuffer> VulkanBufferCache::Acquire(VulkanBufferBinding binding, uint32_t numBytes) noexcept {
    LOG_ASSERT(binding != VulkanBufferBinding::Unknown);

    BufferPool& bufferPool = GetPool(binding);

    // 优先复用容量不小于请求大小的空闲缓冲，避免新建 VkBuffer
    auto iter = bufferPool.lower_bound(numBytes);
    if (iter != bufferPool.end()) {
        VulkanGpuBuffer const* gpuBuffer = iter->second.gpuBuffer;
        bufferPool.erase(iter);
        return m_resourceManager.AllocateAndConstruct<VulkanBuffer>(
            gpuBuffer, [this](VulkanGpuBuffer const* gpuBuffer) { this->Release(gpuBuffer); });
    }

    VulkanGpuBuffer const* gpuBuffer = Allocate(binding, numBytes);
    return m_resourceManager.AllocateAndConstruct<VulkanBuffer>(
        gpuBuffer, [this](VulkanGpuBuffer const* gpuBuffer) { this->Release(gpuBuffer); });
}

void VulkanBufferCache::Gc() noexcept {
    // 前几帧提前返回，避免无符号帧计数回绕
    constexpr uint32_t kTimeBeforeEviction = 3;
    if (++m_currentFrame <= kTimeBeforeEviction) {
        return;
    }
    const uint64_t evictionTime = m_currentFrame - kTimeBeforeEviction;

    for (auto& bufferPool : m_gpuBufferPools) {
        for (auto poolIter = bufferPool.begin(); poolIter != bufferPool.end();) {
            if (poolIter->second.lastAccessed < evictionTime) {
#if BVK_ENABLED(BVK_DEBUG_VULKAN_BUFFER_CACHE)
                LOG_DEBUG("VulkanBufferCache - Destroyed vkBuffer {} with binding {}",
                          poolIter->second.gpuBuffer->vkbuffer, static_cast<int>(poolIter->second.gpuBuffer->binding));
#endif
                Destroy(poolIter->second.gpuBuffer);
                poolIter = bufferPool.erase(poolIter);
            } else {
                ++poolIter;
            }
        }
    }
}

void VulkanBufferCache::Terminate() noexcept {
    for (auto& bufferPool : m_gpuBufferPools) {
        for (auto& poolEntry : bufferPool) {
            Destroy(poolEntry.second.gpuBuffer);
        }
        bufferPool.clear();
    }
}

void VulkanBufferCache::Release(VulkanGpuBuffer const* gpuBuffer) noexcept {
    LOG_ASSERT(gpuBuffer != nullptr);

    BufferPool& bufferPool = GetPool(gpuBuffer->binding);
    bufferPool.insert(std::make_pair(gpuBuffer->numBytes,
                                     UnusedGpuBuffer{ .lastAccessed = m_currentFrame, .gpuBuffer = gpuBuffer }));
}

VulkanGpuBuffer const* VulkanBufferCache::Allocate(VulkanBufferBinding binding, uint32_t numBytes) noexcept {
    VkBufferCreateInfo const bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = numBytes,
        // 需要 TRANSFER_DST 才能经 staging 用 vkCmdCopyBuffer 更新
        .usage = getVkBufferUsage(binding) | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };

    VmaAllocationCreateFlags vmaFlags = 0;
    // UMA 下缓冲恒可映射，标记 HOST_ACCESS 使 pMappedData 有效
    if (m_context.IsUnifiedMemoryArchitecture()) {
        vmaFlags |= VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }

    VulkanGpuBuffer* gpuBuffer = new VulkanGpuBuffer{
        .numBytes = numBytes,
        .binding  = binding,
    };
    VmaAllocationCreateInfo const allocInfo{
        .flags         = vmaFlags,
        .usage         = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };
    VkResult result = vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &gpuBuffer->vkbuffer,
                                      &gpuBuffer->vmaAllocation, &gpuBuffer->allocationInfo);

#if BVK_ENABLED(BVK_DEBUG_VULKAN_BUFFER_CACHE)
    if (result != VK_SUCCESS) {
        LOG_ERROR("VulkanBufferCache - failed to allocate a new vkBuffer of size {} and binding {}, error: {}",
                  numBytes, static_cast<int>(binding), result);
    } else {
        LOG_DEBUG("VulkanBufferCache - allocated a vkBuffer {} of size {} and binding {} successfully",
                  gpuBuffer->vkbuffer, numBytes, static_cast<int>(binding));
    }
#endif

    return gpuBuffer;
}

void VulkanBufferCache::Destroy(VulkanGpuBuffer const* gpuBuffer) noexcept {
    vmaDestroyBuffer(m_allocator, gpuBuffer->vkbuffer, gpuBuffer->vmaAllocation);
    delete gpuBuffer;
}

VulkanBufferCache::BufferPool& VulkanBufferCache::GetPool(VulkanBufferBinding binding) noexcept {
    int poolIndex = -1;
    switch (binding) {
        case VulkanBufferBinding::Vertex:
            poolIndex = 0;
            break;
        case VulkanBufferBinding::Index:
            poolIndex = 1;
            break;
        case VulkanBufferBinding::Uniform:
            poolIndex = 2;
            break;
        case VulkanBufferBinding::ShaderStorage:
            poolIndex = 3;
            break;
        case VulkanBufferBinding::Unknown:
            LOG_CRITICAL("There's no pool for buffers with unknown binding.");
            break;
    }

    LOG_ASSERT(poolIndex >= 0 && poolIndex < kMaxPoolCount);
    return m_gpuBufferPools[poolIndex];
}

END_NS_BACKEND
