#include "vulkan/commands/VulkanCommandBuffer.h"

#include "Utils/Log.h"

BEGIN_NS_BACKEND

namespace {

VkCommandBuffer createCommandBuffer(VkDevice device, VkCommandPool pool) {
    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;

    VkCommandBufferAllocateInfo const allocateInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    // 命令缓冲在 vkBeginCommandBuffer 时被隐式重置，且随命令池销毁一并释放，无需单独回收
    vkAllocateCommandBuffers(device, &allocateInfo, &cmdBuffer);
    return cmdBuffer;
}

}  // namespace

uint32_t VulkanCommandBuffer::s_ageCounter = 0;

VulkanCommandBuffer::VulkanCommandBuffer(const VulkanContextPtr &context, VulkanFencePool &fencePool, VkDevice device, VkQueue queue,
                                         VkCommandPool pool, const VulkanSemaphoreManagerPtr &semaphoreManager)
    : m_context(context),
      m_fencePool(fencePool),
      m_markerCount(0),
      m_queue(queue),
      m_semaphoreManager(semaphoreManager),
      m_buffer(createCommandBuffer(device, pool)),
      m_submission(semaphoreManager->Acquire()),
      m_age(++s_ageCounter) {
    m_fenceStatus = m_fencePool.AcquireFenceStatus();
}

void VulkanCommandBuffer::Reset() noexcept {
    m_markerCount = 0;
    m_resources.clear();
    m_waitSemaphoreCount = 0;
    m_age                = ++s_ageCounter;
    m_submission         = m_semaphoreManager->Acquire();

    // 内部用 VK_INCOMPLETE 表示"尚未提交"，提交后转 VK_NOT_READY，GPU 执行完转 VK_SUCCESS。
    // 旧围栏状态可能仍被其他持有者引用，重置时换新而非复用
    m_fenceStatus = m_fencePool.AcquireFenceStatus();
}

void VulkanCommandBuffer::PushMarker(char const *marker) noexcept {
    if (m_context->IsDebugUtilsSupported()) {
        VkDebugUtilsLabelEXT const labelInfo{
            .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
            .pLabelName = marker,
            .color      = { 0, 1, 0, 1 },
        };
        vkCmdBeginDebugUtilsLabelEXT(m_buffer, &labelInfo);
    } else if (m_context->IsDebugMarkersSupported()) {
        VkDebugMarkerMarkerInfoEXT const markerInfo{
            .sType       = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT,
            .pMarkerName = marker,
            .color       = { 0.0f, 1.0f, 0.0f, 1.0f },
        };
        vkCmdDebugMarkerBeginEXT(m_buffer, &markerInfo);
    }
    m_markerCount++;
}

void VulkanCommandBuffer::PopMarker() noexcept {
    LOG_ASSERT(m_markerCount > 0);

    if (m_context->IsDebugUtilsSupported()) {
        vkCmdEndDebugUtilsLabelEXT(m_buffer);
    } else if (m_context->IsDebugMarkersSupported()) {
        vkCmdDebugMarkerEndEXT(m_buffer);
    }
    m_markerCount--;
}

void VulkanCommandBuffer::InsertEvent(char const *marker) noexcept {
    if (m_context->IsDebugUtilsSupported()) {
        VkDebugUtilsLabelEXT const labelInfo{
            .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
            .pLabelName = marker,
            .color      = { 1, 1, 0, 1 },
        };
        vkCmdInsertDebugUtilsLabelEXT(m_buffer, &labelInfo);
    } else if (m_context->IsDebugMarkersSupported()) {
        VkDebugMarkerMarkerInfoEXT const markerInfo{
            .sType       = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT,
            .pMarkerName = marker,
            .color       = { 0.0f, 1.0f, 0.0f, 1.0f },
        };
        vkCmdDebugMarkerInsertEXT(m_buffer, &markerInfo);
    }
}

void VulkanCommandBuffer::Begin() noexcept {
    VkCommandBufferBeginInfo const beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(m_buffer, &beginInfo);
}

NS_UTILS::SharedPtr<VulkanSemaphore> VulkanCommandBuffer::Submit() {
    while (m_markerCount > 0) {
        PopMarker();
    }

    vkEndCommandBuffer(m_buffer);

    VkSemaphore const submissionSemaphore = m_submission->GetVkSemaphore();
    VkSubmitInfo      submitInfo{
             .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
             .waitSemaphoreCount   = m_waitSemaphoreCount,
             .pWaitSemaphores      = m_waitSemaphores.data(),
             .pWaitDstStageMask    = m_waitSemaphoreStages.data(),
             .commandBufferCount   = 1u,
             .pCommandBuffers      = &m_buffer,
             .signalSemaphoreCount = 1u,
             .pSignalSemaphores    = &submissionSemaphore,
    };

    VkResult const result = vkQueueSubmit(m_queue, 1, &submitInfo, GetVkFence());
    m_fenceStatus->MarkSubmitted();
    LOG_ASSERT(result == VK_SUCCESS);

    m_waitSemaphoreCount = 0;
    return m_submission;
}

END_NS_BACKEND
