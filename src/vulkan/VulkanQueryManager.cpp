#include "vulkan/VulkanQueryManager.h"

#include "vulkan/VulkanAsyncHandles.h"
#include "vulkan/VulkanConstants.h"
#include "vulkan/commands/VulkanCommandBuffer.h"

#include "Utils/Log.h"
#include "Utils/Panic.h"

#include <cstdint>

BEGIN_NS_BACKEND

namespace {
constexpr uint32_t kQueriesPerTimer = 2;  // 每个计时器占一对查询：起、止
}  // namespace

VulkanQueryManager::VulkanQueryManager(VkDevice device) : m_device(device) {
    // 一次建够：计时器个数 × 2 个时间戳查询
    VkQueryPoolCreateInfo createInfo = {
        .sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType  = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = static_cast<uint32_t>(m_used.Size()) * kQueriesPerTimer,
    };
    VkResult const result = vkCreateQueryPool(m_device, &createInfo, kVkAlloc, &m_pool);
    FILAMENT_CHECK_POSTCONDITION(result == VK_SUCCESS) << "vkCreateQueryPool failed." << " error=" << static_cast<int32_t>(result);
}

VulkanTimerQueryPtr VulkanQueryManager::GetNextQuery(const ResourceManagerPtr& resourceManager) {
    size_t firstUnused = 0;  // 取空闲位与置位必须在同一临界区：否则并发取用时可能拿到同一个下标
    {
        std::lock_guard const lock(m_mutex);
        auto const            unused = ~m_used;
        if (unused.Empty()) {
            LOG_ERROR("More than {} timers are not supported.", m_used.Size());
            return {};
        }
        firstUnused = unused.FirstSetBit();
        m_used.Set(firstUnused);
    }
    return resourceManager->AllocateAndConstruct<VulkanTimerQuery>(static_cast<uint32_t>(firstUnused * kQueriesPerTimer),
                                                                  static_cast<uint32_t>(firstUnused * kQueriesPerTimer + 1));
}

void VulkanQueryManager::ClearQuery(VulkanTimerQueryPtr const& query) {
    std::lock_guard const lock(m_mutex);
    uint32_t const        startingIndex = query->GetStartingQueryIndex();
    m_used.Unset(startingIndex / kQueriesPerTimer);
}

void VulkanQueryManager::BeginQuery(VulkanCommandBuffer const* commands, VulkanTimerQueryPtr const& query) {
    uint32_t const index = query->GetStartingQueryIndex();

    VkCommandBuffer const cmdBuffer = commands->Buffer();
    vkCmdResetQueryPool(cmdBuffer, m_pool, index, kQueriesPerTimer);
    vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_pool, index);

    // 记下当时的围栏状态：读回可能发生在查询真正执行之前
    query->SetFence(commands->GetFenceStatus());
}

void VulkanQueryManager::EndQuery(VulkanCommandBuffer const* commands, VulkanTimerQueryPtr const& query) {
    uint32_t const index = query->GetStoppingQueryIndex();
    vkCmdWriteTimestamp(commands->Buffer(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_pool, index);
}

VulkanQueryManager::QueryResult VulkanQueryManager::GetResult(VulkanTimerQueryPtr const& query) {
    uint32_t const index = query->GetStartingQueryIndex();

    QueryResult  result;
    size_t const dataSize = sizeof(result);
    // 起止两项各自的输出跨度为「64 位值 + 64 位可用性标志」
    VkDeviceSize const stride = sizeof(uint64_t) * 2;
    VkResult const     vkResult =
            vkGetQueryPoolResults(m_device, m_pool, index, kQueriesPerTimer, dataSize, static_cast<void*>(&result), stride,
                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    FILAMENT_CHECK_POSTCONDITION(vkResult == VK_SUCCESS || vkResult == VK_NOT_READY)
            << "vkGetQueryPoolResults error=" << static_cast<int32_t>(vkResult);
    if (vkResult == VK_NOT_READY) {
        return {};
    }
    return result;
}

void VulkanQueryManager::Terminate() noexcept {
    if (m_pool == VK_NULL_HANDLE) {
        return;
    }
    vkDestroyQueryPool(m_device, m_pool, kVkAlloc);
    m_pool = VK_NULL_HANDLE;
}

END_NS_BACKEND
