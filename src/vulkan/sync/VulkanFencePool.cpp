#include "vulkan/sync/VulkanFencePool.h"

#include "Utils/Log.h"

#include <algorithm>
#include <functional>

#include "vulkan/VkDef.h"

BEGIN_NS_BACKEND

namespace {
constexpr uint32_t kFenceTimeBeforeEviction = 3;
}  // namespace

VulkanFencePool::VulkanFencePool(const VulkanContextPtr &context, VkDevice device, uint32_t minPoolSize)
    : m_context(context), m_device(device), m_minPoolSize(minPoolSize) {
    for (uint32_t i = 0; i < minPoolSize; ++i) {
        VkFence const fence = AllocateFence();
        if (fence != VK_NULL_HANDLE) {
            m_fences.emplace_back(m_currFrame, fence);
        }
    }
    // 只统计成功分配的围栏
    m_numFences = static_cast<uint32_t>(m_fences.size());
}

std::shared_ptr<VulkanCmdFence> VulkanFencePool::AcquireFenceStatus() noexcept {
    VkFence const                fence       = AcquireFence();
    std::function<void(VkFence)> recycleFn   = [this](VkFence handle) { ReleaseFence(handle); };
    auto const                   fenceStatus = std::make_shared<VulkanCmdFence>(fence, std::move(recycleFn));
    m_fenceStatuses.emplace_back(fenceStatus);
    return fenceStatus;
}

void VulkanFencePool::Gc() noexcept {
    auto const expired =
        std::remove_if(m_fenceStatuses.begin(), m_fenceStatuses.end(), [](auto const &fenceStatus) { return fenceStatus.expired(); });
    m_fenceStatuses.erase(expired, m_fenceStatuses.end());

    // 前几帧不淘汰：刚归还的围栏可能立刻被复用，过早销毁会反复创建
    if (++m_currFrame <= kFenceTimeBeforeEviction) {
        return;
    }

    while (m_numFences > m_minPoolSize && !m_fences.empty() && m_fences.front().first + kFenceTimeBeforeEviction < m_currFrame) {
        DestroyFence(m_fences.front().second);
        m_fences.pop_front();
        --m_numFences;
    }
}

void VulkanFencePool::Terminate() noexcept {
    for (auto const &weakFenceStatus : m_fenceStatuses) {
        if (auto const fenceStatus = weakFenceStatus.lock()) {
            // 切断回池路径，使残余围栏状态对象直接销毁句柄，不再写回已终止的池
            fenceStatus->SwapRecycleFn([device = m_device](VkFence fence) { vkDestroyFence(device, fence, kVkAlloc); });
        }
    }
    m_fenceStatuses.clear();

    for (auto const &fence : m_fences) {
        DestroyFence(fence.second);
    }
    m_fences.clear();
    m_numFences = 0;
}

VkFence VulkanFencePool::AcquireFence() noexcept {
    if (!m_fences.empty()) {
        VkFence const fence = m_fences.back().second;
        m_fences.pop_back();
        return fence;
    }

    VkFence const fence = AllocateFence();
    LOG_ASSERT(fence != VK_NULL_HANDLE);
    ++m_numFences;
    return fence;
}

void VulkanFencePool::ReleaseFence(VkFence fence) noexcept {
    // 仅当分配失败才会走到这里，属严重问题
    LOG_ASSERT(fence != VK_NULL_HANDLE);

    // 归还前必须复位，否则下一次提交会看到已置位状态
    vkResetFences(m_device, 1, &fence);
    m_fences.emplace_back(m_currFrame, fence);
}

VkFence VulkanFencePool::AllocateFence() const noexcept {
    VkFence fence = VK_NULL_HANDLE;

    VkFenceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = VK_NULL_HANDLE,
        .flags = 0,
    };

    VkExportFenceCreateInfo exportFenceCreateInfo{
        .sType       = VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO,
        .handleTypes = m_context->GetFenceExportFlags(),
    };

    // 导出标志为零时链上该结构会让部分实现（如 swiftshader）报错
    if (m_context->GetFenceExportFlags()) {
        createInfo.pNext = &exportFenceCreateInfo;
    }

    VkResult const result = vkCreateFence(m_device, &createInfo, kVkAlloc, &fence);
    if (result != VK_SUCCESS) {
        LOG_ERROR("Failed to create fence: {}", static_cast<int>(result));
        return VK_NULL_HANDLE;
    }
    return fence;
}

void VulkanFencePool::DestroyFence(VkFence fence) const noexcept { vkDestroyFence(m_device, fence, kVkAlloc); }

END_NS_BACKEND
