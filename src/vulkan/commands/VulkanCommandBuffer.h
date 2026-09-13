#pragma once

#include "Backend/DriverDefine.h"

#include "Utils/Utils.h"

#include <array>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "vulkan/VkDef.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/resource/Resource.h"
#include "vulkan/sync/VulkanCmdFence.h"
#include "vulkan/sync/VulkanFencePool.h"
#include "vulkan/sync/VulkanSemaphore.h"
#include "vulkan/sync/VulkanSemaphoreManager.h"

BEGIN_NS_BACKEND

// 提交围栏具备共享所有权语义：可能同时被驱动侧围栏对象与命令缓冲引用，
// 因此不能在某次提交结束时就回收
struct VulkanCommandBuffer {
    VulkanCommandBuffer(const VulkanContextPtr &context, VulkanFencePool &fencePool, VkDevice device, VkQueue queue, VkCommandPool pool,
                        const VulkanSemaphoreManagerPtr &semaphoreManager);

    VulkanCommandBuffer(VulkanCommandBuffer const &)            = delete;
    VulkanCommandBuffer &operator=(VulkanCommandBuffer const &) = delete;

    // 借用资源：录制期间持有引用，保证提交完成前不会被 ResourceManager::Gc 回收
    template <typename T, typename = std::enable_if_t<std::is_base_of_v<Resource, T>>>
    inline void Acquire(NS_UTILS::SharedPtr<T> resource) {
        // SharedPtr 无派生类到基类的转换构造，此处显式重建一份引用
        m_resources.emplace_back(resource.Get());
    }

    void Reset() noexcept;

    void InsertWait(VkSemaphore semaphore, VkPipelineStageFlags waitStage) {
        LOG_ASSERT(m_waitSemaphoreCount < kMaxWaitSemaphores);
        m_waitSemaphores[m_waitSemaphoreCount]      = semaphore;
        m_waitSemaphoreStages[m_waitSemaphoreCount] = waitStage;
        ++m_waitSemaphoreCount;
    }

    void PushMarker(char const *marker) noexcept;
    void PopMarker() noexcept;
    void InsertEvent(char const *marker) noexcept;

    void Begin() noexcept;
    NODISCARD NS_UTILS::SharedPtr<VulkanSemaphore> Submit();

    void RefreshStatus(VkDevice device) { m_fenceStatus->RefreshStatus(device); }

    NODISCARD VkResult GetStatus() const { return m_fenceStatus->GetStatus(); }

    NODISCARD std::shared_ptr<VulkanCmdFence> GetFenceStatus() const { return m_fenceStatus; }

    NODISCARD VkFence GetVkFence() const { return m_fenceStatus->GetVkFence(); }

    NODISCARD VkCommandBuffer Buffer() const { return m_buffer; }

    NODISCARD uint32_t Age() const { return m_age; }

private:
    static uint32_t s_ageCounter;

    // 一次提交最多累积两条等待：注入的外部依赖 + 上一提交的完成信号量
    static constexpr uint32_t kMaxWaitSemaphores = 2;

    VulkanContextPtr          m_context;
    VulkanFencePool          &m_fencePool;
    uint8_t                   m_markerCount;
    VkQueue                   m_queue;
    VulkanSemaphoreManagerPtr m_semaphoreManager;

    template <typename T>
    using MaxWaitArray = std::array<T, kMaxWaitSemaphores>;
    MaxWaitArray<VkSemaphore>          m_waitSemaphores{};
    MaxWaitArray<VkPipelineStageFlags> m_waitSemaphoreStages{};
    uint32_t                           m_waitSemaphoreCount = 0;
    VkCommandBuffer                    m_buffer;
    VulkanSemaphorePtr                 m_submission;
    std::shared_ptr<VulkanCmdFence>    m_fenceStatus;
    std::vector<ResourcePtr>           m_resources;
    uint32_t                           m_age;
};

END_NS_BACKEND
