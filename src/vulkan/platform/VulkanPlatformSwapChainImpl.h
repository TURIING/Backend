#pragma once

#include "Backend/platform/VulkanPlatform.h"

#include <unordered_map>

#include "../VkDef.h"
#include "../VulkanContext.h"

BEGIN_NS_BACKEND

struct VulkanPlatformSwapChainBase : public Platform::SwapChain {
    VulkanPlatformSwapChainBase(VulkanContext const &context, VkDevice device, VkQueue queue);

    virtual ~VulkanPlatformSwapChainBase();

    NODISCARD VulkanPlatform::SwapChainBundle GetSwapChainBundle() const;

    virtual VkResult Acquire(VulkanPlatform::ImageSyncData *outImageSyncData) = 0;

    virtual VkResult Present(uint32_t index, VkSemaphore finished) = 0;

    virtual VkResult Recreate() = 0;

    NODISCARD virtual bool HasResized() const = 0;

    NODISCARD virtual bool IsProtected() const = 0;

    NODISCARD virtual bool QueryCompositorTiming(Platform::CompositorTiming *outCompositorTiming) const;

    NODISCARD virtual bool SetPresentFrameId(uint64_t frameId) const;

    NODISCARD virtual bool QueryFrameTimestamps(uint64_t frameId, Platform::FrameTimestamps *outFrameTimestamps) const;

protected:
    virtual void Destroy();

    VkImage CreateImage(VkExtent2D extent, VkFormat format, bool isProtected);

    // 子类 Destroy() 释放自己的资源后须调用基类实现，以回收深度图像与内存映射
    VulkanContext const                         &m_context;
    VkDevice                                     m_device;
    VkQueue                                      m_queue;
    VulkanPlatform::SwapChainBundle              m_swapChainBundle;
    std::unordered_map<VkImage, VkDeviceMemory>  m_memory;
};

struct VulkanPlatformSurfaceSwapChain : public VulkanPlatformSwapChainBase {
    VulkanPlatformSurfaceSwapChain(VulkanContext const &context, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue,
                                   VkInstance instance, VkSurfaceKHR surface, VkExtent2D fallbackExtent, uint64_t flags);

    ~VulkanPlatformSurfaceSwapChain() override;

    VkResult Acquire(VulkanPlatform::ImageSyncData *outImageSyncData) override;

    VkResult Present(uint32_t index, VkSemaphore finished) override;

    VkResult Recreate() override;

    NODISCARD bool HasResized() const override;

    NODISCARD bool IsProtected() const override;

protected:
    void Destroy() override;

    NODISCARD bool QueryCompositorTiming(Platform::CompositorTiming *outCompositorTiming) const override;

    NODISCARD bool SetPresentFrameId(uint64_t frameId) const override;

    NODISCARD bool QueryFrameTimestamps(uint64_t frameId, Platform::FrameTimestamps *outFrameTimestamps) const override;

private:
    // acquire 用的信号量数量；取命令缓冲上限，避免同一信号量被连续两帧复用
    static constexpr uint32_t kImageReadySemaphoreCount = kMaxCommandBuffers;

    VkResult Create();

    VkInstance       m_instance;
    VkPhysicalDevice m_physicalDevice;
    VkSurfaceKHR     m_surface;  // 本类接管 surface 的所有权
    VkSwapchainKHR   m_swapchain = VK_NULL_HANDLE;
    VkExtent2D const m_fallbackExtent;  // surface capabilities 未定义 currentExtent 时使用
    VkSemaphore      m_imageReady[kImageReadySemaphoreCount];
    uint32_t         m_currentImageReadyIndex = 0;

    bool const m_usesRGB     = false;
    bool const m_hasStencil  = false;
    bool const m_isProtected = false;
    bool       m_suboptimal  = false;
};

struct VulkanPlatformHeadlessSwapChain : public VulkanPlatformSwapChainBase {
    static constexpr uint32_t kHeadlessSwapChainSize = 2;  // 无 surface 时的双缓冲下限

    VulkanPlatformHeadlessSwapChain(VulkanContext const &context, VkDevice device, VkQueue queue, VkExtent2D extent, uint64_t flags);

    ~VulkanPlatformHeadlessSwapChain() override;

    VkResult Acquire(VulkanPlatform::ImageSyncData *outImageSyncData) override;

    VkResult Present(uint32_t index, VkSemaphore finished) override;

    VkResult Recreate() override;

    NODISCARD bool HasResized() const override;

    NODISCARD bool IsProtected() const override;

protected:
    void Destroy() override;

private:
    uint32_t m_currentIndex = 0;
};

END_NS_BACKEND
