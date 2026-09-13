#pragma once

#include "Backend/platform/VulkanPlatform.h"

#include "HwDefine.h"
#include "VkDef.h"
#include "VulkanContext.h"
#include "VulkanTexture.h"
#include "resource/Resource.h"
#include "stage/VulkanStagePool.h"
#include "sync/VulkanSemaphore.h"

#include "Utils/Utils.h"

#include <cstdint>
#include <utility>
#include <vector>

BEGIN_NS_BACKEND

class VulkanCommands;

DECLARE_SHARE_PTR_CLASS(VulkanSwapChain);

// 平台交换链的包装：把平台侧 SwapChainBundle 里的 VkImage 转成 VulkanTexture 附件，
// 并封装图像获取、呈现与重建
struct VulkanSwapChain : public HwSwapChain, public Resource {
    VulkanSwapChain(const VulkanPlatformPtr& platform, const VulkanContextPtr& context, const ResourceManagerPtr& resourceManager,
                    VmaAllocator allocator, VulkanCommands* commands, const VulkanStagePoolPtr& stagePool, void* nativeWindow, uint64_t flags,
                    VkExtent2D extent = { 0, 0 });

    ~VulkanSwapChain() override;

    VulkanSwapChain(VulkanSwapChain const&)            = delete;
    VulkanSwapChain& operator=(VulkanSwapChain const&) = delete;

    // 取下一张可用于渲染的图像；返回 {是否获取成功, 底层图像是否已更换}
    std::pair<bool, bool> Acquire();

    // 呈现最后获取的那张图像；headless 交换链只提交命令，不真正呈现
    void Present();

    // 当前获取到的颜色附件；未获取任何图像时下标无意义
    NODISCARD VulkanTexturePtr GetCurrentColor() const noexcept;

    NODISCARD VulkanTexturePtr GetDepth() const noexcept { return m_depth; }

    NODISCARD bool IsFirstRenderPass() const noexcept { return m_isFirstRenderPass; }

    void MarkFirstRenderPass() noexcept { m_isFirstRenderPass = false; }

    NODISCARD VkExtent2D GetExtent() const noexcept { return m_extent; }

    NODISCARD bool IsProtected() const noexcept;

private:
    // 图像数上限取命令缓冲上限，避免同一信号量被连续两帧复用
    static constexpr uint32_t kImageReadySemaphoreCount = kMaxCommandBuffers;

    // 依据当前 SwapChainBundle 重建全部附件
    void Update();

    VulkanPlatformPtr  m_platform;
    VulkanContextPtr   m_context;
    ResourceManagerPtr m_resourceManager;
    VulkanCommands*    m_commands;
    VmaAllocator       m_allocator;
    VulkanStagePoolPtr m_stagePool;
    bool const         m_headless;
    bool const         m_flushAndWaitOnResize;
    bool const         m_transitionSwapChainImageLayoutForPresent;

    // 附件由本对象持有，平台只提供 VkImage
    std::vector<VulkanTexturePtr>   m_colors;
    std::vector<VulkanSemaphorePtr> m_finishedDrawing;
    VulkanTexturePtr                m_depth;
    VkExtent2D                      m_extent{};
    uint32_t                        m_layerCount;
    uint32_t                        m_currentSwapIndex;
    bool                            m_acquired;
    bool                            m_isFirstRenderPass;
};

END_NS_BACKEND
