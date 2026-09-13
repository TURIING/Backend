#pragma once

#include "Utils/Utils.h"

#include <chrono>
#include <list>
#include <utility>

#include "vulkan/VkDef.h"

BEGIN_NS_BACKEND

#if BVK_ENABLED(BVK_DEBUG_GROUP_MARKERS)
// 调试标记栈：跨命令缓冲边界维持标记层级，并把时间戳带到新缓冲
class VulkanGroupMarkers {
public:
    using Timestamp = std::chrono::time_point<std::chrono::high_resolution_clock>;

    void Push(NS_UTILS::String const &marker, Timestamp start = {}) noexcept;
    std::pair<NS_UTILS::String, Timestamp> Pop() noexcept;
    std::pair<NS_UTILS::String, Timestamp> PopBottom() noexcept;
    std::pair<NS_UTILS::String, Timestamp> const &Top() const;
    bool Empty() const noexcept;

private:
    std::list<std::pair<NS_UTILS::String, Timestamp>> m_markers;
};
#endif  // BVK_ENABLED(BVK_DEBUG_GROUP_MARKERS)

END_NS_BACKEND
