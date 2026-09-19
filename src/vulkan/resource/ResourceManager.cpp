#include "vulkan/resource/ResourceManager.h"

#include "vulkan/VkDef.h"
#include "vulkan/VulkanAsyncHandles.h"
#include "vulkan/VulkanDescriptorSetCache.h"
#include "vulkan/VulkanFboCache.h"
#include "vulkan/VulkanHandle.h"
#include "vulkan/VulkanSwapChain.h"
#include "vulkan/VulkanTexture.h"
#include "vulkan/buffer/VulkanBuffer.h"
#include "vulkan/stage/VulkanStageBuffer.h"
#include "vulkan/stage/VulkanStagePool.h"
#include "vulkan/sync/VulkanSemaphore.h"

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
    GcList threadSafeList;  // 先排空线程安全队列：其对象可能被普通队列对象的析构路径引用
    {
        std::lock_guard<std::mutex> lock(m_threadSafeGcListMutex);
        threadSafeList.swap(m_threadSafeGcList);
    }
    for (auto const& [type, id] : threadSafeList) {
        destroyWithType(type, id);
    }

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
        // 两把锁分开取：避免嵌套持锁，也避免与 destructLaterWithType 的单锁路径产生锁序问题
        bool threadSafeEmpty = false;
        {
            std::lock_guard<std::mutex> lock(m_threadSafeGcListMutex);
            threadSafeEmpty = m_threadSafeGcList.empty();
        }
        bool empty = false;
        {
            std::lock_guard<std::mutex> lock(m_gcListMutex);
            empty = m_gcList.empty();
        }
        if (threadSafeEmpty && empty) {
            break;
        }
        Gc();
    }
}

void ResourceManager::destroyWithType(ResourceType type, HandleBase::HandleId id) {
    switch (type) {
        case ResourceType::BufferObject:
            destruct<VulkanBufferObject>(Handle<VulkanBufferObject>(id));
            break;
        case ResourceType::IndexBuffer:
            destruct<VulkanIndexBuffer>(Handle<VulkanIndexBuffer>(id));
            break;
        case ResourceType::VertexBuffer:
            destruct<VulkanVertexBuffer>(Handle<VulkanVertexBuffer>(id));
            break;
        case ResourceType::VertexBufferInfo:
            destruct<VulkanVertexBufferInfo>(Handle<VulkanVertexBufferInfo>(id));
            break;
        case ResourceType::VulkanBuffer:
            destruct<VulkanBuffer>(Handle<VulkanBuffer>(id));
            break;
        case ResourceType::StageSegment:
            destruct<VulkanStageBuffer::Segment>(Handle<VulkanStageBuffer::Segment>(id));
            break;
        case ResourceType::Semaphore:
            destruct<VulkanSemaphore>(Handle<VulkanSemaphore>(id));
            break;
        case ResourceType::StageImage:
            destruct<VulkanStageImage::Resource>(Handle<VulkanStageImage::Resource>(id));
            break;
        case ResourceType::Texture:
            destruct<VulkanTexture>(Handle<VulkanTexture>(id));
            break;
        case ResourceType::TextureState:
            destruct<VulkanTextureState>(Handle<VulkanTextureState>(id));
            break;
        case ResourceType::SwapChain:
            destruct<VulkanSwapChain>(Handle<VulkanSwapChain>(id));
            break;
        case ResourceType::RenderTarget:
            destruct<VulkanRenderTarget>(Handle<VulkanRenderTarget>(id));
            break;
        case ResourceType::Framebuffer:
            destruct<VulkanFramebuffer>(Handle<VulkanFramebuffer>(id));
            break;
        case ResourceType::RenderPass:
            destruct<VulkanRenderPass>(Handle<VulkanRenderPass>(id));
            break;
        case ResourceType::Program:
            destruct<VulkanProgram>(Handle<VulkanProgram>(id));
            break;
        case ResourceType::Fence:
            destruct<VulkanFence>(Handle<VulkanFence>(id));
            break;
        case ResourceType::Sync:
            destruct<VulkanSync>(Handle<VulkanSync>(id));
            break;
        case ResourceType::TimerQuery:
            destruct<VulkanTimerQuery>(Handle<VulkanTimerQuery>(id));
            break;
        case ResourceType::DescriptorSetLayout:
            destruct<VulkanDescriptorSetLayout>(Handle<VulkanDescriptorSetLayout>(id));
            break;
        case ResourceType::DescriptorSet:
            destruct<VulkanDescriptorSet>(Handle<VulkanDescriptorSet>(id));
            break;
        case ResourceType::RenderPrimitive:
            destruct<VulkanRenderPrimitive>(Handle<VulkanRenderPrimitive>(id));
            break;
        case ResourceType::MemoryMappedBuffer:
            destruct<VulkanMemoryMappedBuffer>(Handle<VulkanMemoryMappedBuffer>(id));
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

size_t ResourceManager::GetPendingGcCount() const noexcept {
    std::lock_guard<std::mutex> const lock(m_gcListMutex);
    return m_gcList.size();
}

size_t ResourceManager::GetPendingThreadSafeGcCount() const noexcept {
    std::lock_guard<std::mutex> const lock(m_threadSafeGcListMutex);
    return m_threadSafeGcList.size();
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
    // 线程安全类型的归零可能发生在编译线程或 app 线程，与 backend 线程的 Gc() 并发；
    // 独立锁使 Gc() 排空普通队列时不与这些线程争锁
    if (IsThreadSafeType(type)) {
        std::lock_guard<std::mutex> lock(m_threadSafeGcListMutex);
        m_threadSafeGcList.push_back({ type, id });
        return;
    }
    std::lock_guard<std::mutex> lock(m_gcListMutex);
    m_gcList.push_back({ type, id });
}

END_NS_BACKEND
