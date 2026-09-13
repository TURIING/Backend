#include "vulkan/sync/VulkanSemaphore.h"

#include "vulkan/sync/VulkanSemaphoreManager.h"

BEGIN_NS_BACKEND

VulkanSemaphore::VulkanSemaphore(const VulkanSemaphoreManagerPtr &manager, VkSemaphore semaphore) : m_manager(manager), m_semaphore(semaphore) {}

VulkanSemaphore::~VulkanSemaphore() { m_manager->Recycle(m_semaphore); }

END_NS_BACKEND
