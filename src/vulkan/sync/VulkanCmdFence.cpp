#include "vulkan/sync/VulkanCmdFence.h"

BEGIN_NS_BACKEND

VulkanCmdFence::VulkanCmdFence(VkFence fence, std::function<void(VkFence)> recycleFn) : m_fence(fence), m_recycleFn(std::move(recycleFn)) {}

VulkanCmdFence::~VulkanCmdFence() {
    if (m_recycleFn) {
        m_recycleFn(m_fence);
    }
}

std::shared_ptr<VulkanCmdFence> VulkanCmdFence::Completed() noexcept {
    auto const fence = std::make_shared<VulkanCmdFence>(VK_NULL_HANDLE);
    fence->m_status  = VK_SUCCESS;
    return fence;
}

void VulkanCmdFence::RefreshStatus(VkDevice device) {
    if (m_fence != VK_NULL_HANDLE) {
        VkResult const status = vkGetFenceStatus(device, m_fence);
        if (status == VK_SUCCESS) {
            SetStatus(status);
        }
    }
}

FenceStatus VulkanCmdFence::Wait(VkDevice device, uint64_t const timeout, std::chrono::steady_clock::time_point const until) {
    {
        std::shared_lock lock(m_lock);

        // 尚未提交时 vkWaitForFences 没有意义，先等提交把状态推进到 VK_NOT_READY
        if (m_status == VK_INCOMPLETE) {
            bool const signaled = m_cond.wait_until(lock, until, [this] { return m_status != VK_INCOMPLETE || m_canceled; });
            if (!signaled) {
                return m_canceled ? FenceStatus::Error : FenceStatus::TimeoutExpired;
            }
        }

        if (m_status == VK_SUCCESS) {
            return FenceStatus::ConditionSatisfied;
        }

        if (m_canceled) {
            return FenceStatus::Error;
        }
    }

    // 不持锁等待：否则 RefreshStatus/SetStatus 无法推进状态
    VkResult const status = vkWaitForFences(device, 1, &m_fence, VK_TRUE, timeout);
    if (status == VK_TIMEOUT) {
        return FenceStatus::TimeoutExpired;
    }
    if (status == VK_SUCCESS) {
        SetStatus(status);
        return FenceStatus::ConditionSatisfied;
    }
    return FenceStatus::Error;
}

void VulkanCmdFence::Cancel() {
    std::lock_guard const lock(m_lock);
    m_canceled = true;
    m_cond.notify_all();
}

END_NS_BACKEND
