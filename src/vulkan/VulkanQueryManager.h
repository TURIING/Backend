#pragma once

#include "Backend/DriverDefine.h"

#include "Utils/Bitset.h"
#include "Utils/Macro.h"

#include <cstdint>
#include <mutex>

#include "vulkan/VkDef.h"
#include "vulkan/VulkanAsyncHandles.h"
#include "vulkan/resource/ResourceManager.h"

BEGIN_NS_BACKEND

struct VulkanCommandBuffer;

// 计时查询池。
//
// 池是一整块 VkQueryPool：每个计时器占相邻的两个查询（起、止）。已分配的计时器用位掩码
// 标记，释放后位被清掉即可复用同一对查询。
class VulkanQueryManager : public NS_UTILS::Ref {
public:
    // 与 vkGetQueryPoolResults 的读取布局一一对应：每项 64 位值加一个 64 位可用性标志
    struct QueryResult {
        uint64_t beginTime      = 0;
        uint64_t beginAvailable = 0;
        uint64_t endTime        = 0;
        uint64_t endAvailable   = 0;
    };

    explicit VulkanQueryManager(VkDevice device);
    ~VulkanQueryManager() = default;

    VulkanQueryManager(VulkanQueryManager const&)            = delete;
    VulkanQueryManager& operator=(VulkanQueryManager const&) = delete;

    // 取一个空闲计时器；池已满时返回空
    NODISCARD VulkanTimerQueryPtr GetNextQuery(const ResourceManagerPtr& resourceManager);

    void ClearQuery(VulkanTimerQueryPtr const& query);

    void BeginQuery(VulkanCommandBuffer const* commands, VulkanTimerQueryPtr const& query);

    void EndQuery(VulkanCommandBuffer const* commands, VulkanTimerQueryPtr const& query);

    // 查询尚未完成时返回全零结果，调用方据可用性标志判断
    NODISCARD QueryResult GetResult(VulkanTimerQueryPtr const& query);

    // 须在 VkDevice 销毁前调用；语义幂等
    void Terminate() noexcept;

private:
    VkDevice m_device;
    VkQueryPool m_pool = VK_NULL_HANDLE;
    NS_UTILS::Bitset32 m_used;  // 每一位对应一个计时器（即一对查询）是否已分配
    std::mutex         m_mutex;
};

DECLARE_SHARE_PTR_CLASS(VulkanQueryManager);

END_NS_BACKEND
