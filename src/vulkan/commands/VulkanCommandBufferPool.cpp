#include "vulkan/commands/VulkanCommandBufferPool.h"

#include "Utils/Log.h"

#include <utility>

BEGIN_NS_BACKEND

VulkanCommandBufferPool::VulkanCommandBufferPool(const VulkanContextPtr &context, VkDevice device, VkQueue queue, uint8_t queueFamilyIndex,
                                                 const VulkanSemaphoreManagerPtr &semaphoreManager)
    : m_device(device), m_recording(kInvalid), m_fencePool(context, device, kCapacity) {
    VkCommandPoolCreateInfo const createInfo{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = queueFamilyIndex,
    };
    vkCreateCommandPool(device, &createInfo, kVkAlloc, &m_pool);

    m_buffers.reserve(kCapacity);
    for (int i = 0; i < kCapacity; ++i) {
        m_buffers.emplace_back(std::make_unique<VulkanCommandBuffer>(context, m_fencePool, device, queue, m_pool, semaphoreManager));
    }
}

VulkanCommandBufferPool::~VulkanCommandBufferPool() {
    Wait();
    Gc();
    vkDestroyCommandPool(m_device, m_pool, kVkAlloc);
    m_fencePool.Terminate();
}

VulkanCommandBuffer &VulkanCommandBufferPool::GetRecording() {
    if (IsRecording()) {
        return *m_buffers[m_recording];
    }

    auto const findNext = [this]() -> int8_t {
        for (int8_t i = 0; i < kCapacity; ++i) {
            if (!m_submitted[i]) {
                return i;
            }
        }
        return kInvalid;
    };

    while ((m_recording = findNext()) == kInvalid) {
        Wait();
        Gc();
    }

    auto &recording = *m_buffers[m_recording];
    recording.Begin();

#if BVK_ENABLED(BVK_DEBUG_GROUP_MARKERS)
    if (m_groupMarkers) {
        auto markers = std::make_unique<VulkanGroupMarkers>();
        // 自底向上重建，保证新缓冲里的标记嵌套关系与概念栈一致
        while (!m_groupMarkers->Empty()) {
            auto [marker, timestamp] = m_groupMarkers->PopBottom();
            recording.PushMarker(marker.c_str());
            markers->Push(marker, timestamp);
        }
        std::swap(m_groupMarkers, markers);
    }
#endif

    return recording;
}

void VulkanCommandBufferPool::Gc() {
    ActiveBuffers reclaimed;
    for (size_t index = 0; index < kCapacity; ++index) {
        if (!m_submitted.test(index)) {
            continue;
        }
        auto &buffer = *m_buffers[index];
        if (buffer.GetStatus() == VK_SUCCESS) {
            reclaimed.set(index, true);
            buffer.Reset();
        }
    }
    m_submitted &= ~reclaimed;
    m_fencePool.Gc();
}

void VulkanCommandBufferPool::Update() {
    for (size_t index = 0; index < kCapacity; ++index) {
        if (m_submitted.test(index)) {
            m_buffers[index]->RefreshStatus(m_device);
        }
    }
}

NS_UTILS::SharedPtr<VulkanSemaphore> VulkanCommandBufferPool::Flush() {
    if (!IsRecording()) {
        return {};
    }
    auto submitSemaphore = m_buffers[m_recording]->Submit();
    m_submitted.set(m_recording, true);
    m_recording = kInvalid;
    return submitSemaphore;
}

void VulkanCommandBufferPool::Wait() {
    uint32_t count = 0;
    VkFence  fences[kCapacity];
    for (size_t index = 0; index < kCapacity; ++index) {
        if (m_submitted.test(index)) {
            fences[count++] = m_buffers[index]->GetVkFence();
        }
    }
    if (count) {
        vkWaitForFences(m_device, count, fences, VK_TRUE, UINT64_MAX);
    }
    Update();
}

void VulkanCommandBufferPool::WaitFor(VkSemaphore previousAction, VkPipelineStageFlags waitStage) {
    if (!IsRecording()) {
        return;
    }
    m_buffers[m_recording]->InsertWait(previousAction, waitStage);
}

#if BVK_ENABLED(BVK_DEBUG_GROUP_MARKERS)
NS_UTILS::String VulkanCommandBufferPool::TopMarker() const {
    if (!m_groupMarkers || m_groupMarkers->Empty()) {
        return "";
    }
    return std::get<0>(m_groupMarkers->Top());
}

void VulkanCommandBufferPool::PushMarker(char const *marker, VulkanGroupMarkers::Timestamp timestamp) {
    if (!m_groupMarkers) {
        m_groupMarkers = std::make_unique<VulkanGroupMarkers>();
    }
    m_groupMarkers->Push(NS_UTILS::String{ marker }, timestamp);
    GetRecording().PushMarker(marker);
}

std::pair<NS_UTILS::String, VulkanGroupMarkers::Timestamp> VulkanCommandBufferPool::PopMarker() {
    LOG_ASSERT(m_groupMarkers && !m_groupMarkers->Empty());
    auto ret = m_groupMarkers->Pop();

    // 未录制时只弹概念栈：命令缓冲里没有对应的结束标记可写
    if (IsRecording()) {
        GetRecording().PopMarker();
    }
    return ret;
}

void VulkanCommandBufferPool::InsertEvent(char const *marker) { GetRecording().InsertEvent(marker); }
#endif  // BVK_ENABLED(BVK_DEBUG_GROUP_MARKERS)

END_NS_BACKEND
