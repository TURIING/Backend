#pragma once

#include "Utils/Macro.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <utility>
#include <vector>

#include "vulkan/VulkanContext.h"
#include "vulkan/sync/VulkanCmdFence.h"

BEGIN_NS_BACKEND

// VkFence 生命周期管理；非线程安全，取用与归还必须发生在同一 backend 线程
class VulkanFencePool {
public:
    VulkanFencePool(const VulkanContextPtr &context, VkDevice device, uint32_t minPoolSize);
    ~VulkanFencePool() = default;

    VulkanFencePool(const VulkanFencePool &)            = delete;
    VulkanFencePool &operator=(const VulkanFencePool &) = delete;

    // 返回的围栏状态对象析构时把 VkFence 归还本池
    NODISCARD std::shared_ptr<VulkanCmdFence> AcquireFenceStatus() noexcept;

    // 清除已失效的围栏状态并把长时间空闲的围栏还给驱动
    void Gc() noexcept;

    // 把存活围栏状态对象的回收路径改为直接销毁，再释放池内全部空闲围栏
    void Terminate() noexcept;

private:
    NODISCARD VkFence AcquireFence() noexcept;
    void ReleaseFence(VkFence fence) noexcept;
    NODISCARD VkFence AllocateFence() const noexcept;
    void DestroyFence(VkFence fence) const noexcept;

    VulkanContextPtr m_context;
    VkDevice const   m_device;
    uint32_t const   m_minPoolSize;

    // 帧号 → 空闲围栏，帧号用于淘汰判定
    std::deque<std::pair<uint64_t, VkFence>> m_fences;
    // 仅弱引用跟踪，用于终止时切断回池路径
    std::vector<std::weak_ptr<VulkanCmdFence>> m_fenceStatuses;

    uint32_t m_numFences = 0;
    uint64_t m_currFrame = 0;
};

END_NS_BACKEND
