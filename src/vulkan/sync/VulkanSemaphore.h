#pragma once

#include "Backend/DriverDefine.h"

#include "Utils/Utils.h"

#include "vulkan/resource/Resource.h"

BEGIN_NS_BACKEND

class VulkanSemaphoreManager;
DECLARE_SHARE_PTR_CLASS(VulkanSemaphoreManager);

// 提交信号量的资源包装：析构即把 VkSemaphore 归还其池，而非销毁句柄
struct VulkanSemaphore : public Resource {
public:
    VulkanSemaphore(const VulkanSemaphoreManagerPtr &manager, VkSemaphore semaphore);
    ~VulkanSemaphore() override;

    NODISCARD VkSemaphore GetVkSemaphore() const { return m_semaphore; }

private:
    VulkanSemaphoreManagerPtr m_manager;
    VkSemaphore               m_semaphore;
};

DECLARE_SHARE_PTR_CLASS(VulkanSemaphore);

END_NS_BACKEND
