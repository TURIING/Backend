#pragma once

#include "Backend/TargetBufferInfo.h"

#include "VkDef.h"
#include "resource/Resource.h"
#include "resource/ResourceManager.h"

#include "Utils/Hash.h"
#include "Utils/Utils.h"

#include <cstdint>
#include <unordered_map>

BEGIN_NS_BACKEND

struct VulkanRenderTarget;
struct VulkanRenderPass;
struct VulkanFramebuffer;

DECLARE_SHARE_PTR_CLASS(VulkanRenderTarget);
DECLARE_SHARE_PTR_CLASS(VulkanRenderPass);
DECLARE_SHARE_PTR_CLASS(VulkanFramebuffer);

// VkRenderPass 与 VkFramebuffer 的对象缓存。
//
// VkFramebuffer 只是「渲染通道 + 一组图像视图」的绑定，两者都不占显存，故缓存的是对象
// 本身而非离屏渲染表面。键的复用度直接决定渲染通道的创建次数。
class VulkanFboCache {
public:
    constexpr static VulkanLayout kFinalColorAttachmentLayout        = VulkanLayout::COLOR_ATTACHMENT;
    constexpr static VulkanLayout kFinalResolveAttachmentLayout      = VulkanLayout::COLOR_ATTACHMENT;
    constexpr static VulkanLayout kFinalDepthStencilAttachmentLayout = VulkanLayout::DEPTH_STENCIL_ATTACHMENT;

    // 构造一个 VkRenderPass 所需的全部不变状态；按字节哈希后作为查找键
    struct alignas(8) RenderPassKey {
        VkFormat          colorFormat[MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT];  // 32 字节
        VkFormat          depthStencilFormat;                                   // 4 字节
        TargetBufferFlags clear;                                                // 4 字节
        TargetBufferFlags discardStart;                                         // 4 字节
        TargetBufferFlags discardEnd;                                           // 4 字节

        VulkanLayout initialDepthStencilLayout;  // 1 字节
        uint8_t      samples;                    // 1 字节
        uint8_t      needsResolveMask;           // 1 字节
        uint8_t      usesLazilyAllocatedMemory;  // 1 字节
        uint8_t      subpassMask;                // 1 字节
        uint8_t      viewCount;                  // 1 字节
        uint8_t      padding[2];
    };
    struct RenderPassVal {
        VulkanRenderPassPtr handle;
        uint32_t            timestamp;
    };
    static_assert(0 == MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT % 8);
    static_assert(sizeof(TargetBufferFlags) == 4, "TargetBufferFlags has unexpected size.");
    static_assert(sizeof(VkFormat) == 4, "VkFormat has unexpected size.");
    static_assert(sizeof(RenderPassKey) == 56, "RenderPassKey has unexpected size.");

    using RenderPassHash = NS_UTILS::hash::MurmurHashFn<RenderPassKey>;

    struct RenderPassEq {
        bool operator()(RenderPassKey const& k1, RenderPassKey const& k2) const;
    };

    // 配置一个 VkFramebuffer 所需的不变状态；附件个数不定，未用的槽位以空句柄占位，
    // 因此不需要单独的 count 字段
    struct alignas(8) FboKey {
        VkRenderPass renderPass;                                       // 8 字节
        uint16_t     width;                                            // 2 字节
        uint16_t     height;                                           // 2 字节
        uint16_t     layers;                                           // 2 字节
        uint16_t     samples;                                          // 2 字节
        VkImageView  color[MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT];    // 64 字节
        VkImageView  resolve[MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT];  // 64 字节
        VkImageView  depthStencil;                                     // 8 字节
    };
    struct FboVal {
        VulkanFramebufferPtr handle;
        uint32_t             timestamp;
    };
    static_assert(sizeof(VkRenderPass) == 8, "VkRenderPass has unexpected size.");
    static_assert(sizeof(VkImageView) == 8, "VkImageView has unexpected size.");
    static_assert(sizeof(FboKey) == 152, "FboKey has unexpected size.");

    using FboKeyHashFn = NS_UTILS::hash::MurmurHashFn<FboKey>;

    struct FboKeyEqualFn {
        bool operator()(FboKey const& k1, FboKey const& k2) const;
    };

    VulkanFboCache(VkDevice device, uint32_t timeBeforeEvictionFbo);
    ~VulkanFboCache();

    VulkanFboCache(VulkanFboCache const&)            = delete;
    VulkanFboCache& operator=(VulkanFboCache const&) = delete;

    // 取或创建 VkFramebuffer
    NODISCARD VulkanFramebufferPtr GetFramebuffer(FboKey const& config, const ResourceManagerPtr& resManager,
                                                  VulkanRenderTargetPtr renderTarget) noexcept;

    // 取或创建 VkRenderPass
    NODISCARD VulkanRenderPassPtr GetRenderPass(RenderPassKey const& config, const ResourceManagerPtr& resManager) noexcept;

    // 每帧调用一次，逐出长期未用的对象
    void Gc() noexcept;

    // 交换链重建后调用：图像视图已失效，全部 framebuffer 必须作废
    void ResetFramebuffers() noexcept;

    // 须在 VkDevice 销毁前调用
    void Terminate() noexcept;

private:
    using FboMap        = std::unordered_map<FboKey, FboVal, FboKeyHashFn, FboKeyEqualFn>;
    using RenderPassMap = std::unordered_map<RenderPassKey, RenderPassVal, RenderPassHash, RenderPassEq>;

    VkDevice m_device;
    FboMap   m_framebufferCache;

    RenderPassMap                              m_renderPassCache;
    std::unordered_map<VkRenderPass, uint32_t> m_renderPassRefCount;
    uint32_t                                   m_currentTime = 0;
    uint32_t                                   m_timeBeforeEvictionFbo;
};

// 持 VkRenderPass 的资源对象：由缓存创建，析构即销毁句柄
struct VulkanRenderPass : public Resource {
    VulkanRenderPass(VkDevice device, VkRenderPass renderPass);
    ~VulkanRenderPass() override;

    VulkanRenderPass(VulkanRenderPass const&)            = delete;
    VulkanRenderPass& operator=(VulkanRenderPass const&) = delete;

    NODISCARD VkRenderPass GetVkRenderPass() const noexcept { return m_renderPass; }

private:
    VkDevice     m_device;
    VkRenderPass m_renderPass;
};

// 持 VkFramebuffer 的资源对象：由缓存创建，析构即销毁句柄
struct VulkanFramebuffer : public Resource {
    VulkanFramebuffer(VkDevice device, VkFramebuffer framebuffer, VulkanRenderTargetPtr renderTarget);
    ~VulkanFramebuffer() override;

    VulkanFramebuffer(VulkanFramebuffer const&)            = delete;
    VulkanFramebuffer& operator=(VulkanFramebuffer const&) = delete;

    NODISCARD VkFramebuffer GetVkFramebuffer() const noexcept { return m_framebuffer; }

private:
    VkDevice      m_device;
    VkFramebuffer m_framebuffer;

    // 缓存键里存的是由渲染目标纹理派生的图像视图，故必须让其纹理存活到本对象析构
    VulkanRenderTargetPtr m_renderTarget;
};

END_NS_BACKEND
