#include "VulkanSwapChain.h"

#include "commands/VulkanCommands.h"

#include "Utils/Log.h"

BEGIN_NS_BACKEND

VulkanSwapChain::VulkanSwapChain(const VulkanPlatformPtr& platform, const VulkanContextPtr& context, const ResourceManagerPtr& resourceManager,
                                 VmaAllocator allocator, VulkanCommands* commands, const VulkanStagePoolPtr& stagePool, void* nativeWindow,
                                 uint64_t flags, VkExtent2D extent)
    : m_platform(platform),
      m_context(context),
      m_resourceManager(resourceManager),
      m_commands(commands),
      m_allocator(allocator),
      m_stagePool(stagePool),
      // 有尺寸、无原生窗口即 headless；headless 交换链不接 surface
      m_headless(extent.width != 0 && extent.height != 0 && nativeWindow == nullptr),
      m_flushAndWaitOnResize(platform->GetCustomization().flushAndWaitOnWindowResize),
      m_transitionSwapChainImageLayoutForPresent(platform->GetCustomization().transitionSwapChainImageLayoutForPresent),
      m_layerCount(1),
      m_currentSwapIndex(0),
      m_acquired(false),
      m_isFirstRenderPass(true) {
    swapChain = m_platform->CreateSwapChain(nativeWindow, flags, extent);
    LOG_ASSERT(swapChain != nullptr);

    Update();
}

VulkanSwapChain::~VulkanSwapChain() {
    // 在销毁图像前必须等 GPU 停下：在飞的命令缓冲可能仍在引用交换链附件
    if (m_commands != nullptr) {
        m_commands->Flush();
        m_commands->Wait();
    }

    m_colors.clear();
    m_depth.Reset();
    for (auto& semaphore : m_finishedDrawing) {
        semaphore.Reset();
    }
    m_finishedDrawing.clear();
    m_platform->Destroy(swapChain);
}

void VulkanSwapChain::Update() {
    m_colors.clear();

    auto const   bundle         = m_platform->GetSwapChainBundle(swapChain);
    size_t const swapChainCount = bundle.colors.size();
    m_colors.reserve(swapChainCount);
    VkDevice const device = m_platform->GetVkDevice();

    m_finishedDrawing.clear();
    m_finishedDrawing.reserve(swapChainCount);
    m_finishedDrawing.resize(swapChainCount);

    TextureUsage depthUsage = TextureUsage::DEPTH_ATTACHMENT;
    TextureUsage colorUsage = TextureUsage::COLOR_ATTACHMENT;
    if (bundle.isProtected) {
        depthUsage = depthUsage | TextureUsage::PROTECTED;
        colorUsage = colorUsage | TextureUsage::PROTECTED;
    }

    // 交换链图像归平台所有，这里只包装成纹理以复用其布局跟踪与图像视图缓存
    for (auto const color : bundle.colors) {
        VulkanTexturePtr colorTexture = m_resourceManager->AllocateAndConstruct<VulkanTexture>(
            m_context, device, m_allocator, m_resourceManager, m_commands, color, VK_NULL_HANDLE, bundle.colorFormat, VK_NULL_HANDLE, 1,
            bundle.extent.width, bundle.extent.height, bundle.layerCount, colorUsage, m_stagePool);
        m_colors.push_back(colorTexture);
    }

    m_depth = m_resourceManager->AllocateAndConstruct<VulkanTexture>(m_context, device, m_allocator, m_resourceManager, m_commands, bundle.depth,
                                                                     VK_NULL_HANDLE, bundle.depthFormat, VK_NULL_HANDLE, 1, bundle.extent.width,
                                                                     bundle.extent.height, bundle.layerCount, depthUsage, m_stagePool);

    m_extent     = bundle.extent;
    m_layerCount = bundle.layerCount;
}

void VulkanSwapChain::Present() {
    // 上一次获取失败，无可呈现的图像
    if (!m_acquired) {
        return;
    }

    if (m_commands == nullptr) {
        m_acquired          = false;
        m_isFirstRenderPass = true;
        return;
    }

    if (!m_headless && m_transitionSwapChainImageLayoutForPresent) {
        VulkanCommandBuffer&          commands = m_commands->Get();
        VkImageSubresourceRange const subresources{
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = m_layerCount,
        };
        m_colors[m_currentSwapIndex]->TransitionLayout(&commands, subresources, VulkanLayout::PRESENT);
    }

    m_commands->Flush();

    if (!m_headless) {
        VulkanSemaphorePtr finishedDrawing    = m_commands->AcquireFinishedSignal();
        m_finishedDrawing[m_currentSwapIndex] = finishedDrawing;
        VkResult const result                 = m_platform->Present(swapChain, m_currentSwapIndex, finishedDrawing->GetVkSemaphore());
        LOG_ASSERT(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR);
    }

    m_acquired          = false;
    m_isFirstRenderPass = true;
}

std::pair<bool, bool> VulkanSwapChain::Acquire() {
    // 底层交换链是否已更换；更换意味着缓存里的图像视图全部失效
    bool swapchainRecreated = false;

    VkResult result = VK_NOT_READY;

    // 每次 makeCurrent 都会调用本函数，已获取过图像时直接返回
    if (m_acquired) {
        return { m_acquired, swapchainRecreated };
    }

    if (m_platform->HasResized(swapChain)) {
        result = VK_ERROR_OUT_OF_DATE_KHR;
    }

    VulkanPlatform::ImageSyncData imageSyncData;

    // 循环覆盖两种情况：尺寸变化时先重建再获取；获取返回 suboptimal / out-of-date 时重建后重试一次
    for (uint8_t tryCount = 0; result != VK_SUCCESS && tryCount < 2; tryCount++) {
        if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR) {
            // 重建会替换底层图像，须先让在飞的命令结束
            if (m_flushAndWaitOnResize && m_commands != nullptr) {
                m_commands->Flush();
                m_commands->Wait();
            }
            m_platform->Recreate(swapChain);
            Update();
            swapchainRecreated = true;
        }
        result = m_platform->Acquire(swapChain, &imageSyncData);
    }

    if (result != VK_SUCCESS) {
        // 不置 m_acquired，随后的一次 Present 会因无图像可呈现而跳过
        LOG_DEBUG("Failed to acquire next image in the swapchain result={}", static_cast<int>(result));
        return { false, swapchainRecreated };
    }

    m_currentSwapIndex = imageSyncData.imageIndex;
    LOG_ASSERT(m_currentSwapIndex < m_finishedDrawing.size());
    m_finishedDrawing[m_currentSwapIndex].Reset();

    // 注入依赖：下一次提交须等待该图像就绪，否则可能渲染到尚未可写的图像
    if (m_commands != nullptr && imageSyncData.imageReadySemaphore != VK_NULL_HANDLE) {
        m_commands->InjectDependency(imageSyncData.imageReadySemaphore, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    }
    m_acquired = true;

    return { true, swapchainRecreated };
}

VulkanTexturePtr VulkanSwapChain::GetCurrentColor() const noexcept {
    LOG_ASSERT(m_currentSwapIndex != VulkanPlatform::ImageSyncData::INVALID_IMAGE_INDEX);
    return m_colors[m_currentSwapIndex];
}

bool VulkanSwapChain::IsProtected() const noexcept { return m_platform->IsProtected(swapChain); }

END_NS_BACKEND
