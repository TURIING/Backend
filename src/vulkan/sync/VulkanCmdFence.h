#pragma once

#include "Backend/DriverDefine.h"

#include "Utils/Macro.h"
#include "Utils/Utils.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>

BEGIN_NS_BACKEND

class VulkanFencePool;

// 围栏状态以 shared_ptr 共享持有：可能同时被驱动侧对象与命令缓冲引用，
// 必须等最后一处引用释放才回收底层 VkFence
struct VulkanCmdFence {
    explicit VulkanCmdFence(VkFence fence, std::function<void(VkFence)> recycleFn = nullptr);
    ~VulkanCmdFence();

    // 未提交任何命令即视为全部完成，故持有空句柄但状态为 VK_SUCCESS
    NODISCARD static std::shared_ptr<VulkanCmdFence> Completed() noexcept;

    // 围栏已随提交进入队列，状态由 VK_INCOMPLETE 转为 VK_NOT_READY
    void MarkSubmitted() { SetStatus(VK_NOT_READY); }

    void RefreshStatus(VkDevice device);

    NODISCARD VkFence GetVkFence() const { return m_fence; }

    NODISCARD VkResult GetStatus() const {
        std::shared_lock const lock(m_lock);
        return m_status;
    }

    FenceStatus Wait(VkDevice device, uint64_t timeout, std::chrono::steady_clock::time_point until);

    void Cancel();

private:
    // 生命周期由 VulkanFencePool 管理：池终止时经 SwapRecycleFn 把 VkFence 所有权转移给本对象
    friend class VulkanFencePool;

    void SetStatus(VkResult status) {
        std::lock_guard const lock(m_lock);
        m_status = status;
        m_cond.notify_all();
    }

    void SwapRecycleFn(std::function<void(VkFence)> recycleFn) { m_recycleFn = std::move(recycleFn); }

    mutable std::shared_mutex    m_lock;
    std::condition_variable_any  m_cond;
    bool                         m_canceled = false;
    VkResult                     m_status{ VK_INCOMPLETE };
    VkFence const                m_fence;
    std::function<void(VkFence)> m_recycleFn;
};

END_NS_BACKEND
