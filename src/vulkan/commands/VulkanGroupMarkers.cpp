#include "vulkan/commands/VulkanGroupMarkers.h"

#include "Utils/Log.h"

#include <utility>

BEGIN_NS_BACKEND

#if BVK_ENABLED(BVK_DEBUG_GROUP_MARKERS)
void VulkanGroupMarkers::Push(NS_UTILS::String const &marker, Timestamp start) noexcept {
    m_markers.push_back({ marker, start.time_since_epoch().count() > 0 ? start : std::chrono::high_resolution_clock::now() });
}

std::pair<NS_UTILS::String, VulkanGroupMarkers::Timestamp> VulkanGroupMarkers::Pop() noexcept {
    auto ret = m_markers.back();
    m_markers.pop_back();
    return ret;
}

std::pair<NS_UTILS::String, VulkanGroupMarkers::Timestamp> VulkanGroupMarkers::PopBottom() noexcept {
    auto ret = m_markers.front();
    m_markers.pop_front();
    return ret;
}

std::pair<NS_UTILS::String, VulkanGroupMarkers::Timestamp> const &VulkanGroupMarkers::Top() const {
    LOG_ASSERT(!Empty());
    return m_markers.back();
}

bool VulkanGroupMarkers::Empty() const noexcept { return m_markers.empty(); }
#endif  // BVK_ENABLED(BVK_DEBUG_GROUP_MARKERS)

END_NS_BACKEND
