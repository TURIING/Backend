#pragma once

#include "Backend/DriverDefine.h"
#include "Backend/platform/VulkanPlatform.h"

#include "Utils/Utils.h"

#include <bitset>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "vulkan/VkDef.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/commands/VulkanCommandBuffer.h"
#include "vulkan/commands/VulkanGroupMarkers.h"
#include "vulkan/sync/VulkanFencePool.h"
#include "vulkan/sync/VulkanSemaphore.h"
#include "vulkan/sync/VulkanSemaphoreManager.h"

BEGIN_NS_BACKEND

// 一组命令缓冲 + 其围栏池：调用方只需 GetRecording/Flush，不必关心槽位轮转
class VulkanCommandBufferPool {
public:
    using ActiveBuffers              = std::bitset<kMaxCommandBuffers>;
    static constexpr int8_t kInvalid = -1;

    VulkanCommandBufferPool(const VulkanPlatformPtr &platform, const VulkanContextPtr &context,
                            const VulkanSemaphoreManagerPtr &semaphoreManager);
    ~VulkanCommandBufferPool();

    VulkanCommandBufferPool(const VulkanCommandBufferPool &)            = delete;
    VulkanCommandBufferPool &operator=(const VulkanCommandBufferPool &) = delete;

    NODISCARD VulkanCommandBuffer &GetRecording();

    void Gc();
    void Update();
    NODISCARD NS_UTILS::SharedPtr<VulkanSemaphore> Flush();
    void Wait();
    void WaitFor(VkSemaphore previousAction, VkPipelineStageFlags waitStage);

    NODISCARD bool IsRecording() const { return m_recording != kInvalid; }

#if BVK_ENABLED(BVK_DEBUG_GROUP_MARKERS)
    NODISCARD NS_UTILS::String TopMarker() const;
    void PushMarker(char const *marker, VulkanGroupMarkers::Timestamp timestamp);
    std::pair<NS_UTILS::String, VulkanGroupMarkers::Timestamp> PopMarker();
    void InsertEvent(char const *marker);
#endif

private:
    static constexpr int kCapacity = kMaxCommandBuffers;
    // 槽位索引用 int8_t，容量不得超过其上限
    static_assert(kCapacity < 128);

    VkDevice                                          m_device;
    VkCommandPool                                     m_pool;
    ActiveBuffers                                     m_submitted;
    std::vector<std::unique_ptr<VulkanCommandBuffer>> m_buffers;
    int8_t                                            m_recording;
    VulkanFencePool                                   m_fencePool;

#if BVK_ENABLED(BVK_DEBUG_GROUP_MARKERS)
    std::unique_ptr<VulkanGroupMarkers> m_groupMarkers;
#endif
};

END_NS_BACKEND
