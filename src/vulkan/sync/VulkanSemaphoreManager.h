#pragma once

#include "Backend/DriverDefine.h"

#include "Utils/Utils.h"

#include <cstdint>
#include <vector>

#include "vulkan/resource/ResourceManager.h"
#include "vulkan/sync/VulkanSemaphore.h"

BEGIN_NS_BACKEND

// VkSemaphore 池化：回收而非销毁，避免每帧提交都创建信号量
//
// 必须堆上构造并经 VulkanSemaphoreManagerPtr 持有：产出的每个 VulkanSemaphore 都持一份本对象引用，
// 最后一个引用归零会 delete this；栈上对象会被信号量的析构回调写到已失效的地址上
class VulkanSemaphoreManager : public NS_UTILS::Ref {
public:
    VulkanSemaphoreManager(VkDevice device, const ResourceManagerPtr &resourceManager);
    ~VulkanSemaphoreManager() override = default;

    VulkanSemaphoreManager(const VulkanSemaphoreManager &)            = delete;
    VulkanSemaphoreManager &operator=(const VulkanSemaphoreManager &) = delete;

    // 须在 VkDevice 仍存活、且全部已取出的信号量归还后调用
    void Terminate();

    NODISCARD NS_UTILS::SharedPtr<VulkanSemaphore> Acquire();

private:
    friend struct VulkanSemaphore;

    void Recycle(VkSemaphore semaphore);

    VkDevice                 m_device;
    ResourceManagerPtr       m_resourceManager;
    std::vector<VkSemaphore> m_pool;
};

DECLARE_SHARE_PTR_CLASS(VulkanSemaphoreManager);

END_NS_BACKEND
