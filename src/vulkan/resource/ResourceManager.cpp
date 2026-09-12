#include "vulkan/resource/ResourceManager.h"

#include "vulkan/VkDef.h"
#include "vulkan/VulkanHandle.h"
#include "vulkan/buffer/VulkanBuffer.h"
#include "vulkan/stage/VulkanStageBuffer.h"

#include <utility>

BEGIN_NS_BACKEND

namespace {
#if BVK_ENABLED(BVK_DEBUG_RESOURCE_LEAK)
uint32_t sResourceCounter[static_cast<size_t>(ResourceType::UndefinedType)] = {};
#endif
}  // namespace

ResourceManager::ResourceManager(size_t arenaSize, bool disableUseAfterFreeCheck, bool disablePoolHandleTags)
    : m_handleAllocator("Handles", arenaSize, disableUseAfterFreeCheck, disablePoolHandleTags) {}

void ResourceManager::Gc() noexcept {
    GcList list;
    {
        std::lock_guard<std::mutex> lock(m_gcListMutex);
        list.swap(m_gcList);
    }
    for (auto const& [type, id] : list) {
        destroyWithType(type, id);
    }
}

void ResourceManager::Terminate() noexcept {
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(m_gcListMutex);
            if (m_gcList.empty()) {
                break;
            }
        }
        Gc();
    }
}

void ResourceManager::destroyWithType(ResourceType type, HandleBase::HandleId id) {
    switch (type) {
        case ResourceType::VulkanBuffer:
            destruct<VulkanBuffer>(Handle<VulkanBuffer>(id));
            break;
        case ResourceType::VertexBufferInfo:
            destruct<VulkanVertexBufferInfo>(Handle<VulkanVertexBufferInfo>(id));
            break;
        case ResourceType::StageSegment:
            destruct<VulkanStageBuffer::Segment>(Handle<VulkanStageBuffer::Segment>(id));
            break;
        default:
            break;
    }
#if BVK_ENABLED(BVK_DEBUG_RESOURCE_LEAK)
    sResourceCounter[static_cast<size_t>(type)]--;
#endif
}

void ResourceManager::traceConstruction([[maybe_unused]] ResourceType type, [[maybe_unused]] HandleBase::HandleId id) {
#if BVK_ENABLED(BVK_DEBUG_RESOURCE_LEAK)
    LOG_ASSERT(type != ResourceType::UndefinedType);
    sResourceCounter[static_cast<size_t>(type)]++;
#endif
}

void ResourceManager::Print() const noexcept {
#if BVK_ENABLED(BVK_DEBUG_RESOURCE_LEAK)
    LOG_ERROR("-------------------");
    for (size_t i = 0; i < static_cast<size_t>(ResourceType::UndefinedType); ++i) {
        LOG_ERROR("    {}={}", TransResourceTypeToStr(static_cast<ResourceType>(i)), sResourceCounter[i]);
    }
    LOG_ERROR("+++++++++++++++++++");
#endif
}

void ResourceManager::AssociateTagToHandle(HandleBase::HandleId id, NS_UTILS::ImmutableString&& tag) noexcept {
    m_handleAllocator.AssociateTagToHandle(id, std::move(tag));
}

void ResourceManager::destructLaterWithType(ResourceType type, HandleBase::HandleId id) {
    std::lock_guard<std::mutex> lock(m_gcListMutex);
    m_gcList.push_back({ type, id });
}

END_NS_BACKEND
