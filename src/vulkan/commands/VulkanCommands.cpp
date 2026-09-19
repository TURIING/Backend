#include "vulkan/commands/VulkanCommands.h"

#include "Utils/Log.h"

#include <utility>

BEGIN_NS_BACKEND

VulkanCommands::VulkanCommands(const VulkanPlatformPtr &platform, const VulkanContextPtr &context,
                               const VulkanSemaphoreManagerPtr &semaphoreManager)
    : m_device(platform->GetVkDevice()),
      m_context(context),
      m_semaphoreManager(semaphoreManager),
      m_pool(std::make_unique<VulkanCommandBufferPool>(platform, context, semaphoreManager)) {}

void VulkanCommands::Terminate() {
    m_pool.reset();
    m_lastSubmit.Reset();
    m_lastFenceStatus.reset();
}

VulkanCommandBuffer &VulkanCommands::Get() { return m_pool->GetRecording(); }

bool VulkanCommands::Flush() {
    // 终止之后仍可能被调用，此时无池可提交
    if (!m_pool) {
        return false;
    }

    if (!m_pool->IsRecording()) {
        return true;
    }

    if (m_injectedDependency != VK_NULL_HANDLE) {
        m_pool->WaitFor(m_injectedDependency, m_injectedDependencyWaitStage);
    }
    if (m_lastSubmit) {
        // 只等上一提交的片元输出与传输，放行顶点阶段以增加顶点/片元重叠
        m_pool->WaitFor(m_lastSubmit->GetVkSemaphore(), VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT);
        m_lastSubmit.Reset();
    }

    auto const flushedFenceStatus = m_pool->GetRecording().GetFenceStatus();
    auto       dependency         = m_pool->Flush();

    m_injectedDependency = VK_NULL_HANDLE;
    m_lastSubmit         = dependency;
    m_lastFenceStatus    = flushedFenceStatus;

    return true;
}

void VulkanCommands::Wait() {
    if (!m_pool) {
        return;
    }
    m_pool->Wait();
}

void VulkanCommands::Gc() {
    if (!m_pool) {
        return;
    }
    m_pool->Gc();
}

void VulkanCommands::UpdateFences() {
    if (!m_pool) {
        return;
    }
    m_pool->Update();
}

#if BVK_ENABLED(BVK_DEBUG_GROUP_MARKERS)
void VulkanCommands::PushGroupMarker(char const *marker, VulkanGroupMarkers::Timestamp timestamp) { m_pool->PushMarker(marker, timestamp); }

void VulkanCommands::PopGroupMarker() { m_pool->PopMarker(); }

void VulkanCommands::InsertEventMarker(char const *marker) { m_pool->InsertEvent(marker); }

NS_UTILS::String VulkanCommands::GetTopGroupMarker() const { return m_pool->TopMarker(); }
#endif  // BVK_ENABLED(BVK_DEBUG_GROUP_MARKERS)

END_NS_BACKEND
