#pragma once

#include "Backend/DriverDefine.h"

#include "Utils/Utils.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "vulkan/VulkanContext.h"
#include "vulkan/commands/VulkanCommandBuffer.h"
#include "vulkan/commands/VulkanCommandBufferPool.h"
#include "vulkan/commands/VulkanGroupMarkers.h"
#include "vulkan/sync/VulkanCmdFence.h"
#include "vulkan/sync/VulkanSemaphore.h"
#include "vulkan/sync/VulkanSemaphoreManager.h"

BEGIN_NS_BACKEND

// 命令录制的对外门面：屏蔽命令缓冲与信号量的原始 Vulkan 细节
//
// - 用 VkSemaphore 串起已提交命令缓冲的依赖链，保证按序执行，信号量循环复用
// - 允许注入一个外部依赖信号量（如交换链图像获取），暂停下一次提交
// - 允许取走最近一次提交的完成信号量（供 vkQueuePresentKHR 使用）
// - 围栏状态可在非渲染线程查询：提交线程经 UpdateFences 把状态搬进围栏对象
class VulkanCommands {
public:
    VulkanCommands(VkDevice device, VkQueue queue, uint32_t queueFamilyIndex, const VulkanContextPtr &context,
                   const VulkanSemaphoreManagerPtr &semaphoreManager);

    VulkanCommands(const VulkanCommands &)            = delete;
    VulkanCommands &operator=(const VulkanCommands &) = delete;

    // 须在 VkDevice 仍存活时调用
    void Terminate();

    // 取当前录制中的命令缓冲，不存在则新建
    NODISCARD VulkanCommandBuffer &Get();

    // 提交已录制的命令缓冲；无命令时返回 false
    bool Flush();

    // 取走最近一次提交的完成信号量，并把它从依赖链上摘除
    NODISCARD NS_UTILS::SharedPtr<VulkanSemaphore> AcquireFinishedSignal() {
        auto semaphore = m_lastSubmit;
        m_lastSubmit.Reset();
        return semaphore;
    }

    NODISCARD std::shared_ptr<VulkanCmdFence> GetMostRecentFenceStatus() const { return m_lastFenceStatus; }

    // 注入一个外部依赖信号量，使下一次提交等待它（每次提交只允许注入一个）
    void InjectDependency(VkSemaphore next, VkPipelineStageFlags waitStage) {
        m_injectedDependency          = next;
        m_injectedDependencyWaitStage = waitStage;
    }

    void Gc();
    void Wait();
    void UpdateFences();

#if BVK_ENABLED(BVK_DEBUG_GROUP_MARKERS)
    void PushGroupMarker(char const *marker, VulkanGroupMarkers::Timestamp timestamp = {});
    void PopGroupMarker();
    void InsertEventMarker(char const *marker);
    NODISCARD NS_UTILS::String GetTopGroupMarker() const;
#endif

private:
    VkDevice const            m_device;
    VulkanContextPtr          m_context;
    VulkanSemaphoreManagerPtr m_semaphoreManager;

    std::unique_ptr<VulkanCommandBufferPool> m_pool;

    VkSemaphore m_injectedDependency = VK_NULL_HANDLE;

    VulkanSemaphorePtr m_lastSubmit;

    // 初值取"已完成"：尚未提交任何命令时，按定义全部工作都已完成
    std::shared_ptr<VulkanCmdFence> m_lastFenceStatus = VulkanCmdFence::Completed();

    VkPipelineStageFlags m_injectedDependencyWaitStage = 0;
};

END_NS_BACKEND
