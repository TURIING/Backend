#include "VulkanFboCache.h"

#include "VulkanConstants.h"
#include "VulkanHandle.h"
#include "vulkan/utils/Image.h"

#include "Utils/Log.h"

#include <utility>

BEGIN_NS_BACKEND

namespace {

constexpr uint32_t kTimeBeforeEviction = kMaxCommandBuffers;  // VkRenderPass / VkFramebuffer 闲置超过该帧数即从缓存逐出

}  // namespace

bool VulkanFboCache::RenderPassEq::operator()(RenderPassKey const& k1, RenderPassKey const& k2) const {
    if (k1.initialDepthStencilLayout != k2.initialDepthStencilLayout) {
        return false;
    }
    for (int i = 0; i < MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT; i++) {
        if (k1.colorFormat[i] != k2.colorFormat[i]) {
            return false;
        }
    }
    if (k1.depthStencilFormat != k2.depthStencilFormat) {
        return false;
    }
    if (k1.clear != k2.clear) {
        return false;
    }
    if (k1.discardStart != k2.discardStart) {
        return false;
    }
    if (k1.discardEnd != k2.discardEnd) {
        return false;
    }
    if (k1.samples != k2.samples) {
        return false;
    }
    if (k1.needsResolveMask != k2.needsResolveMask) {
        return false;
    }
    if (k1.usesLazilyAllocatedMemory != k2.usesLazilyAllocatedMemory) {
        return false;
    }
    if (k1.subpassMask != k2.subpassMask) {
        return false;
    }
    if (k1.viewCount != k2.viewCount) {
        return false;
    }
    return true;
}

bool VulkanFboCache::FboKeyEqualFn::operator()(FboKey const& k1, FboKey const& k2) const {
    if (k1.renderPass != k2.renderPass) {
        return false;
    }
    if (k1.width != k2.width) {
        return false;
    }
    if (k1.height != k2.height) {
        return false;
    }
    if (k1.layers != k2.layers) {
        return false;
    }
    if (k1.samples != k2.samples) {
        return false;
    }
    if (k1.depthStencil != k2.depthStencil) {
        return false;
    }
    for (int i = 0; i < MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT; i++) {
        if (k1.color[i] != k2.color[i]) {
            return false;
        }
        if (k1.resolve[i] != k2.resolve[i]) {
            return false;
        }
    }
    return true;
}

VulkanFboCache::VulkanFboCache(VkDevice device, uint32_t timeBeforeEvictionFbo) : m_device(device), m_timeBeforeEvictionFbo(timeBeforeEvictionFbo) {}

VulkanFboCache::~VulkanFboCache() {
    // 缓存必须在 VkDevice 存活期间显式清空，否则析构时销毁句柄已失效
    LOG_ASSERT(m_framebufferCache.empty() && m_renderPassCache.empty());
}

VulkanFramebufferPtr VulkanFboCache::GetFramebuffer(FboKey const& config, const ResourceManagerPtr& resManager,
                                                    VulkanRenderTargetPtr renderTarget) noexcept {
    if (auto const iter = m_framebufferCache.find(config); iter != m_framebufferCache.end()) {
        iter->second.timestamp = m_currentTime;
        return iter->second.handle;
    }

    // 附件顺序为「颜色附件、解析附件、深度模板附件」，须与 GetRenderPass 里建 subpass 的顺序一致，
    // 否则渲染通道与帧缓冲的附件对不上
    VkImageView attachments[MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT + MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT + 1];
    uint32_t    attachmentCount = 0;
    for (VkImageView attachment : config.color) {
        if (attachment) {
            attachments[attachmentCount++] = attachment;
        }
    }
    for (VkImageView attachment : config.resolve) {
        if (attachment) {
            attachments[attachmentCount++] = attachment;
        }
    }
    if (config.depthStencil) {
        attachments[attachmentCount++] = config.depthStencil;
    }

#if BVK_ENABLED(BVK_DEBUG_FBO_CACHE)
    LOG_DEBUG("Creating framebuffer {}x{}, samples={}, attachmentCount={}", config.width, config.height, int(config.samples), attachmentCount);
#endif

    VkFramebufferCreateInfo const info{
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass      = config.renderPass,
        .attachmentCount = attachmentCount,
        .pAttachments    = attachments,
        .width           = config.width,
        .height          = config.height,
        .layers          = config.layers,
    };
    m_renderPassRefCount[info.renderPass]++;
    VkFramebuffer  framebuffer = VK_NULL_HANDLE;
    VkResult const error       = vkCreateFramebuffer(m_device, &info, kVkAlloc, &framebuffer);
    LOG_ASSERT(error == VK_SUCCESS);

    VulkanFramebufferPtr fbh   = resManager->AllocateAndConstruct<VulkanFramebuffer>(m_device, framebuffer, renderTarget);
    m_framebufferCache[config] = { fbh, m_currentTime };
    return fbh;
}

VulkanRenderPassPtr VulkanFboCache::GetRenderPass(RenderPassKey const& config, const ResourceManagerPtr& resManager) noexcept {
    if (auto const iter = m_renderPassCache.find(config); iter != m_renderPassCache.end()) {
        iter->second.timestamp = m_currentTime;
        return iter->second.handle;
    }
    bool const hasSubpasses = config.subpassMask != 0;

    // 以下三个别名只为缩短后续初始化的书写
    VkAttachmentLoadOp const  kClear        = VK_ATTACHMENT_LOAD_OP_CLEAR;
    VkAttachmentLoadOp const  kDontCare     = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentLoadOp const  kKeep         = VK_ATTACHMENT_LOAD_OP_LOAD;
    VkAttachmentStoreOp const kDisableStore = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    VkAttachmentStoreOp const kEnableStore  = VK_ATTACHMENT_STORE_OP_STORE;

    // subpass 描述里给出渲染通道开始时转到的布局，附件描述里给出结束时转到的布局

    VkAttachmentReference inputAttachmentRef[MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT]     = {};
    VkAttachmentReference colorAttachmentRefs[2][MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT] = {};
    VkAttachmentReference resolveAttachmentRef[MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT]   = {};
    VkAttachmentReference depthStencilAttachmentRef                                      = {};

    bool const hasDepthOrStencil = VK_UTILS::IsVkDepthFormat(config.depthStencilFormat) || VK_UTILS::IsVkStencilFormat(config.depthStencilFormat);

    VkSubpassDescription subpasses[2] = { { .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            .pInputAttachments       = nullptr,
                                            .pColorAttachments       = colorAttachmentRefs[0],
                                            .pResolveAttachments     = resolveAttachmentRef,
                                            .pDepthStencilAttachment = hasDepthOrStencil ? &depthStencilAttachmentRef : nullptr },
                                          { .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            .pInputAttachments       = inputAttachmentRef,
                                            .pColorAttachments       = colorAttachmentRefs[1],
                                            .pResolveAttachments     = resolveAttachmentRef,
                                            .pDepthStencilAttachment = hasDepthOrStencil ? &depthStencilAttachmentRef : nullptr } };

    // 附件顺序同 GetFramebuffer；数组按最大可能附件数预留
    VkAttachmentDescription attachments[MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT + MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT + 1] = {};

    // 支持 2 个 subpass，故只需 1 条依赖
    VkSubpassDependency dependencies[1] = { { .srcSubpass      = 0,
                                              .dstSubpass      = 1,
                                              .srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                              .dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                              .srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                              .dstAccessMask   = VK_ACCESS_SHADER_READ_BIT,
                                              .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT } };

    VkRenderPassCreateInfo renderPassInfo{ .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                           .attachmentCount = 0u,
                                           .pAttachments    = attachments,
                                           .subpassCount    = hasSubpasses ? 2u : 1u,
                                           .pSubpasses      = subpasses,
                                           .dependencyCount = hasSubpasses ? 1u : 0u,
                                           .pDependencies   = dependencies };

    VkRenderPassMultiviewCreateInfo multiviewCreateInfo = {};
    uint32_t const                  subpassViewMask     = (1u << config.viewCount) - 1u;
    // 视图掩码数组按最大 subpass 数准备，每个 subpass 都激活全部视图
    uint32_t const viewMasks[2] = { subpassViewMask, subpassViewMask };
    if (config.viewCount > 1) {
        multiviewCreateInfo.sType                = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO;
        multiviewCreateInfo.pNext                = nullptr;
        multiviewCreateInfo.subpassCount         = hasSubpasses ? 2u : 1u;
        multiviewCreateInfo.pViewMasks           = viewMasks;
        multiviewCreateInfo.dependencyCount      = 0;
        multiviewCreateInfo.pViewOffsets         = nullptr;
        multiviewCreateInfo.correlationMaskCount = 1;
        multiviewCreateInfo.pCorrelationMasks    = &subpassViewMask;

        renderPassInfo.pNext = &multiviewCreateInfo;
    }

    int attachmentIndex = 0;

    for (int i = 0; i < MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT; i++) {
        if (config.colorFormat[i] == VK_FORMAT_UNDEFINED) {
            continue;
        }
        VkImageLayout const subpassLayout = VK_UTILS::GetVkLayout(VulkanLayout::COLOR_ATTACHMENT);
        uint32_t            index;

        if (!hasSubpasses) {
            index                                    = subpasses[0].colorAttachmentCount++;
            colorAttachmentRefs[0][index].layout     = subpassLayout;
            colorAttachmentRefs[0][index].attachment = attachmentIndex;
        } else {
            // 驱动 API 把两个 subpass 的颜色附件合并成一个列表，并用位掩码标出只属于第二个
            // subpass、需要在其中作为输入回读的附件；第一个 subpass 的颜色附件自动对第二个可见。
            //
            // 存在 subpass 时要求输入附件必须排在首位，破坏该前提需要先扩展驱动 API
            LOG_ASSERT(config.subpassMask == 1);

            if (config.subpassMask & (1 << i)) {
                index                                    = subpasses[0].colorAttachmentCount++;
                colorAttachmentRefs[0][index].layout     = subpassLayout;
                colorAttachmentRefs[0][index].attachment = attachmentIndex;

                index                                = subpasses[1].inputAttachmentCount++;
                inputAttachmentRef[index].layout     = subpassLayout;
                inputAttachmentRef[index].attachment = attachmentIndex;
            }

            index                                    = subpasses[1].colorAttachmentCount++;
            colorAttachmentRefs[1][index].layout     = subpassLayout;
            colorAttachmentRefs[1][index].attachment = attachmentIndex;
        }

        TargetBufferFlags const flag         = TargetBufferFlags(int(TargetBufferFlags::COLOR0) << i);
        bool const              clear        = HasAnyFlag(config.clear, flag);
        bool const              discardStart = HasAnyFlag(config.discardStart, flag);
        bool const              discardEnd   = HasAnyFlag(config.discardEnd, flag);

        attachments[attachmentIndex++] = {
            .format         = config.colorFormat[i],
            .samples        = (VkSampleCountFlagBits)config.samples,
            .loadOp         = clear ? kClear : (discardStart ? kDontCare : kKeep),
            .storeOp        = (discardEnd || (config.usesLazilyAllocatedMemory & (1 << i))) ? kDisableStore : kEnableStore,
            .stencilLoadOp  = kDontCare,
            .stencilStoreOp = kDisableStore,
            .initialLayout  = VK_UTILS::GetVkLayout(VulkanLayout::COLOR_ATTACHMENT),
            .finalLayout    = VK_UTILS::GetVkLayout(kFinalColorAttachmentLayout),
        };
    }

    // 置空零长列表，否则 Adreno 会返回 VK_ERROR_OUT_OF_HOST_MEMORY
    if (subpasses[0].colorAttachmentCount == 0) {
        subpasses[0].pColorAttachments   = nullptr;
        subpasses[0].pResolveAttachments = nullptr;
        subpasses[1].pColorAttachments   = nullptr;
        subpasses[1].pResolveAttachments = nullptr;
    }

    VkAttachmentReference* pResolveAttachment = resolveAttachmentRef;
    for (int i = 0; i < MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT; i++) {
        if (config.colorFormat[i] == VK_FORMAT_UNDEFINED) {
            continue;
        }

        if (!(config.needsResolveMask & (1 << i))) {
            pResolveAttachment->attachment = VK_ATTACHMENT_UNUSED;
            ++pResolveAttachment;
            continue;
        }

        pResolveAttachment->attachment = attachmentIndex;
        pResolveAttachment->layout     = VK_UTILS::GetVkLayout(VulkanLayout::COLOR_ATTACHMENT_RESOLVE);
        ++pResolveAttachment;

        attachments[attachmentIndex++] = {
            .format         = config.colorFormat[i],
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = kDontCare,
            .storeOp        = kEnableStore,
            .stencilLoadOp  = kDontCare,
            .stencilStoreOp = kDisableStore,
            .initialLayout  = VK_UTILS::GetVkLayout(VulkanLayout::COLOR_ATTACHMENT),
            .finalLayout    = VK_UTILS::GetVkLayout(kFinalColorAttachmentLayout),
        };
    }

    if (hasDepthOrStencil) {
        bool const clearDepth          = HasAnyFlag(config.clear, TargetBufferFlags::DEPTH);
        bool const discardStartDepth   = HasAnyFlag(config.discardStart, TargetBufferFlags::DEPTH);
        bool const discardEndDepth     = HasAnyFlag(config.discardEnd, TargetBufferFlags::DEPTH);
        bool const clearStencil        = HasAnyFlag(config.clear, TargetBufferFlags::STENCIL);
        bool const discardStartStencil = HasAnyFlag(config.discardStart, TargetBufferFlags::STENCIL);
        bool const discardEndStencil   = HasAnyFlag(config.discardEnd, TargetBufferFlags::STENCIL);

        depthStencilAttachmentRef.layout     = VK_UTILS::GetVkLayout(VulkanLayout::DEPTH_STENCIL_ATTACHMENT);
        depthStencilAttachmentRef.attachment = attachmentIndex;
        attachments[attachmentIndex++]       = {
                  .format         = config.depthStencilFormat,
                  .samples        = (VkSampleCountFlagBits)config.samples,
                  .loadOp         = clearDepth ? kClear : (discardStartDepth ? kDontCare : kKeep),
                  .storeOp        = discardEndDepth ? kDisableStore : kEnableStore,
                  .stencilLoadOp  = clearStencil ? kClear : (discardStartStencil ? kDontCare : kKeep),
                  .stencilStoreOp = discardEndStencil ? kDisableStore : kEnableStore,
                  .initialLayout  = VK_UTILS::GetVkLayout(config.initialDepthStencilLayout),
                  .finalLayout    = VK_UTILS::GetVkLayout(kFinalDepthStencilAttachmentLayout),
        };
    }
    renderPassInfo.attachmentCount = attachmentIndex;

    VkRenderPass   renderPass = VK_NULL_HANDLE;
    VkResult const error      = vkCreateRenderPass(m_device, &renderPassInfo, kVkAlloc, &renderPass);
    LOG_ASSERT(error == VK_SUCCESS);
    VulkanRenderPassPtr rph   = resManager->AllocateAndConstruct<VulkanRenderPass>(m_device, renderPass);
    m_renderPassCache[config] = { rph, m_currentTime };

#if BVK_ENABLED(BVK_DEBUG_FBO_CACHE)
    LOG_DEBUG(
        "Created render pass, samples={}, needsResolveMask={}, usesLazilyAllocatedMemory={}, viewCount={}, "
        "colorAttachmentCount={}",
        int(config.samples), int(config.needsResolveMask), int(config.usesLazilyAllocatedMemory), int(config.viewCount),
        subpasses[0].colorAttachmentCount);
#endif

    return rph;
}

void VulkanFboCache::ResetFramebuffers() noexcept {
    for (auto const& pair : m_framebufferCache) {
        m_renderPassRefCount[pair.first.renderPass]--;
    }
    m_framebufferCache.clear();
}

void VulkanFboCache::Terminate() noexcept {
    ResetFramebuffers();

    m_renderPassRefCount.clear();
    m_renderPassCache.clear();
}

void VulkanFboCache::Gc() noexcept {
    ++m_currentTime;

    if (m_currentTime > m_timeBeforeEvictionFbo) {
        uint32_t const evictTimeFbo = m_currentTime - m_timeBeforeEvictionFbo;
        // erase(iterator) 返回后一个元素的迭代器，故不能在循环里按 key 删除
        for (FboMap::iterator iter = m_framebufferCache.begin(); iter != m_framebufferCache.end();) {
            FboVal const fbo = iter->second;
            if (fbo.timestamp < evictTimeFbo && fbo.handle) {
                m_renderPassRefCount[iter->first.renderPass]--;

                iter = m_framebufferCache.erase(iter);
            } else {
                ++iter;
            }
        }
    }

    if (m_currentTime > kTimeBeforeEviction) {
        uint32_t const evictTimeRp = m_currentTime - kTimeBeforeEviction;
        // 同样的迭代器写法；此处额外删除 m_renderPassRefCount 中的项，那是另一个容器
        for (RenderPassMap::iterator iter = m_renderPassCache.begin(); iter != m_renderPassCache.end();) {
            VkRenderPass const handle = iter->second.handle->GetVkRenderPass();
            if (iter->second.timestamp < evictTimeRp && handle && m_renderPassRefCount[handle] == 0) {
                iter = m_renderPassCache.erase(iter);
                m_renderPassRefCount.erase(handle);
            } else {
                ++iter;
            }
        }
    }
}

VulkanFramebuffer::VulkanFramebuffer(VkDevice device, VkFramebuffer framebuffer, VulkanRenderTargetPtr renderTarget)
    : m_device(device), m_framebuffer(framebuffer), m_renderTarget(std::move(renderTarget)) {}

VulkanFramebuffer::~VulkanFramebuffer() { vkDestroyFramebuffer(m_device, m_framebuffer, kVkAlloc); }

VulkanRenderPass::VulkanRenderPass(VkDevice device, VkRenderPass renderPass) : m_device(device), m_renderPass(renderPass) {}

VulkanRenderPass::~VulkanRenderPass() { vkDestroyRenderPass(m_device, m_renderPass, kVkAlloc); }

END_NS_BACKEND
