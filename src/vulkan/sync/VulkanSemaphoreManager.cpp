#include "vulkan/sync/VulkanSemaphoreManager.h"

#include "vulkan/VkDef.h"

BEGIN_NS_BACKEND

namespace {

VkSemaphore createSemaphore(VkDevice device) {
    VkSemaphore                 semaphore = VK_NULL_HANDLE;
    VkSemaphoreCreateInfo const createInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    vkCreateSemaphore(device, &createInfo, kVkAlloc, &semaphore);
    return semaphore;
}

}  // namespace

VulkanSemaphoreManager::VulkanSemaphoreManager(VkDevice device, const ResourceManagerPtr &resourceManager)
    : m_device(device), m_resourceManager(resourceManager) {
    m_pool.reserve(kMaxCommandBuffers);
    for (int i = 0; i < kMaxCommandBuffers; ++i) {
        m_pool.push_back(createSemaphore(m_device));
    }
}

void VulkanSemaphoreManager::Terminate() {
    for (VkSemaphore semaphore : m_pool) {
        vkDestroySemaphore(m_device, semaphore, kVkAlloc);
    }
    m_pool.clear();
}

NS_UTILS::SharedPtr<VulkanSemaphore> VulkanSemaphoreManager::Acquire() {
    VkSemaphore semaphore = VK_NULL_HANDLE;
    if (!m_pool.empty()) {
        semaphore = m_pool.back();
        m_pool.pop_back();
    } else {
        semaphore = createSemaphore(m_device);
    }
    // 让信号量持有一份管理器引用，使其归还路径不会写到已析构的管理器上
    return m_resourceManager->AllocateAndConstruct<VulkanSemaphore>(VulkanSemaphoreManagerPtr(this), semaphore);
}

void VulkanSemaphoreManager::Recycle(VkSemaphore semaphore) { m_pool.push_back(semaphore); }

END_NS_BACKEND
