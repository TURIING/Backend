#include "VulQueue.h"

BEGIN_NS_BACKEND

// Builder 配置数据
struct VulQueue::BuilderDetails {
    VulLogicDevicePtr m_device;
    uint32_t          m_queueFamilyIndex = 0;
    uint32_t          m_queueIndex       = 0;
    bool              m_protected        = false;
};

VulQueue::VulQueue(VkQueue queue) {
    m_pHandle = queue;
}

VulQueue::Builder::Builder() noexcept = default;
VulQueue::Builder::~Builder() noexcept = default;

VulQueue::Builder &VulQueue::Builder::SetDevice(VulLogicDevicePtr device) noexcept {
    m_pImpl->m_device = std::move(device);
    return *this;
}

VulQueue::Builder &VulQueue::Builder::SetQueueFamilyIndex(uint32_t index) noexcept {
    m_pImpl->m_queueFamilyIndex = index;
    return *this;
}

VulQueue::Builder &VulQueue::Builder::SetQueueIndex(uint32_t index) noexcept {
    m_pImpl->m_queueIndex = index;
    return *this;
}

VulQueue::Builder &VulQueue::Builder::SetProtected(bool enabled) noexcept {
    m_pImpl->m_protected = enabled;
    return *this;
}

VulQueuePtr VulQueue::Builder::Build() {
    VkQueue queue = VK_NULL_HANDLE;
    if (m_pImpl->m_protected) {
        VkDeviceQueueInfo2 info = {
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
            .flags            = VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT,
            .queueFamilyIndex = m_pImpl->m_queueFamilyIndex,
            .queueIndex       = m_pImpl->m_queueIndex,
        };
        vkGetDeviceQueue2(m_pImpl->m_device->GetHandle(), &info, &queue);
    } else {
        vkGetDeviceQueue(m_pImpl->m_device->GetHandle(), m_pImpl->m_queueFamilyIndex,
                         m_pImpl->m_queueIndex, &queue);
    }
    return VulQueuePtr(new VulQueue(queue));
}

END_NS_BACKEND