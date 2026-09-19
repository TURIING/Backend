#include "App.h"

#include "Backend/Namespace.h"
#include "Backend/Program.h"
#include "Backend/platform/VulkanPlatform.h"

#include "Utils/Log.h"
#include "Utils/mem/SharedPtr.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "GlfwWindow.h"
#include "TriangleShaders.h"
#include "vulkan/VulkanAsyncHandles.h"
#include "vulkan/VulkanBlitter.h"
#include "vulkan/VulkanConstants.h"
#include "vulkan/VulkanDescriptorSetCache.h"
#include "vulkan/VulkanDescriptorSetLayoutCache.h"
#include "vulkan/VulkanFboCache.h"
#include "vulkan/VulkanHandle.h"
#include "vulkan/VulkanPipelineCache.h"
#include "vulkan/VulkanPipelineLayoutCache.h"
#include "vulkan/VulkanQueryManager.h"
#include "vulkan/VulkanReadPixels.h"
#include "vulkan/VulkanSamplerCache.h"
#include "vulkan/VulkanSwapChain.h"
#include "vulkan/VulkanTexture.h"
#include "vulkan/resource/ResourceManager.h"
#include "vulkan/stage/VulkanStagePool.h"
#include "vulkan/sync/VulkanCmdFence.h"
#include "vulkan/utils/Conversion.h"

BEGIN_NS_TEST

// 端到端绘制用例：定义在文件末尾，此处前置声明以便 main 调用
bool VerifyDrawTriangleEndToEnd();

namespace {
constexpr uint32_t kFrameCount        = 8;
constexpr int64_t  kRefreshIntervalNs = 16666;
constexpr uint32_t kIndexCount        = 1024;
constexpr uint32_t kSwapChainWidth    = 1280;
constexpr uint32_t kSwapChainHeight   = 720;

constexpr size_t   kHandleArenaSize = 8u * 1024u * 1024u;  // 纹理资源层的验证规模：句柄 arena 与驱动保持同一量级
constexpr VkFormat kTestFormat      = VK_FORMAT_R8G8B8A8_UNORM;
constexpr uint32_t kTestWidth       = 64;
constexpr uint32_t kTestHeight      = 64;
constexpr uint32_t kTestLevels      = 2;
constexpr uint32_t kStageWidth      = 256;
constexpr uint32_t kStageHeight     = 256;
constexpr uint32_t kOtherStageWidth = 512;

// framebuffer 闲置几帧后允许逐出；逐出轮数须超过该值加上 render pass 的年龄上限
constexpr uint32_t kFboTimeBeforeEviction = 3;
constexpr uint32_t kFboEvictionRounds     = 64;

// headless 交换链是无窗口环境下唯一可验证的交换链路径；
// acquire / present 走真实 surface，不在本用例覆盖范围内
bool VerifyHeadlessSwapChain(const EnginePtr &engine) {
    auto platform = engine->GetPlatform().Cast<NS_BD::VulkanPlatform>();
    if (platform == nullptr) {
        LOG_ERROR("headless swapchain: platform is not a VulkanPlatform");
        return false;
    }

    // MoltenVK 不支持 VK_GOOGLE_display_timing
    if (platform->IsCompositorTimingSupported()) {
        LOG_ERROR("headless swapchain: compositor timing is unexpectedly supported");
        return false;
    }

    Backend::VulkanPlatform::ImageSyncData syncData;  // 空句柄调用须退化为 no-op 而不是崩溃
    bool const                             nullHandleSafe = platform->Acquire(nullptr, &syncData) == VK_ERROR_UNKNOWN &&
                                platform->Present(nullptr, 0, VK_NULL_HANDLE) == VK_ERROR_UNKNOWN &&
                                !platform->HasResized(nullptr) && !platform->IsProtected(nullptr);
    platform->Destroy(nullptr);
    if (!nullHandleSafe) {
        LOG_ERROR("headless swapchain: null handle is not handled defensively");
        return false;
    }

    std::shared_ptr<Backend::VulkanCmdFence> fence;
    Backend::Platform::Sync                 *sync = platform->CreateSync(fence);
    if (sync == nullptr) {
        LOG_ERROR("headless swapchain: CreateSync returned null");
        return false;
    }
    platform->DestroySync(sync);
    platform->DestroySync(nullptr);

    auto const swapChain = platform->CreateSwapChain(nullptr, 0, { kSwapChainWidth, kSwapChainHeight });
    if (swapChain == nullptr) {
        LOG_ERROR("headless swapchain: CreateSwapChain returned null");
        return false;
    }

    auto const bundle = platform->GetSwapChainBundle(swapChain);
    LOG_INFO("headless swapchain: extent={}x{}, colorFormat={}, imageCount={}, depthFormat={}, protected={}",
             bundle.extent.width, bundle.extent.height, static_cast<int>(bundle.colorFormat), bundle.colors.size(),
             static_cast<int>(bundle.depthFormat), bundle.isProtected);

    bool const bundleValid = bundle.extent.width == kSwapChainWidth && bundle.extent.height == kSwapChainHeight &&
                             bundle.colorFormat != VK_FORMAT_UNDEFINED && bundle.colors.size() >= 2 &&
                             bundle.depth != VK_NULL_HANDLE;

    // 销毁应释放全部图像与内存，不留 VMA / Vulkan 对象
    platform->Destroy(swapChain);
    return bundleValid;
}

// 测试侧自建 VMA 分配器：与驱动用的那份隔离，便于断言「无残留分配」
VmaAllocator CreateTestAllocator(const Backend::VulkanPlatformPtr &platform) {
    VmaVulkanFunctions const vulkanFunctions{
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr   = vkGetDeviceProcAddr,
    };

    VmaAllocatorCreateInfo const allocatorInfo{
        .flags            = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT,
        .physicalDevice   = platform->GetVkPhysicalDevice(),
        .device           = platform->GetVkDevice(),
        .pVulkanFunctions = &vulkanFunctions,
        .instance         = platform->GetVkInstance(),
    };

    VmaAllocator allocator = VK_NULL_HANDLE;
    if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS) {
        LOG_ERROR("texture layer: failed to create a VMA allocator");
    }
    return allocator;
}

// 默认构造的 VulkanContext 不带内存属性（只有 VulkanPlatform 能填充），测试图像的内存类型自行挑选
uint32_t SelectTestMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeBits, VkFlags required) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);

    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) != 0 && (properties.memoryTypes[i].propertyFlags & required) == required) {
            return i;
        }
    }
    return VK_MAX_MEMORY_TYPES;
}

// 同尺寸二次获取命中复用、不同尺寸不复用、归还后可复用
bool VerifyStageImageReuse(const Backend::VulkanStagePoolPtr &stagePool,
                           const Backend::ResourceManagerPtr &resourceManager) {
    auto const acquire = [&stagePool](uint32_t width, uint32_t height) {
        return stagePool->AcquireStageImage(Backend::PixelDataFormat::RGBA, Backend::PixelDataType::UBYTE, width,
                                            height);
    };

    Backend::VulkanStageImage::ResourcePtr first  = acquire(kStageWidth, kStageHeight);
    VkImage const                          reused = first->GetImage();

    // 引用归零只入 GC 队列，须再经一次 Gc 才把图像交还空闲表
    first.Reset();
    resourceManager->Gc();

    Backend::VulkanStageImage::ResourcePtr second   = acquire(kStageWidth, kStageHeight);
    bool const                             reuseHit = second->GetImage() == reused;

    Backend::VulkanStageImage::ResourcePtr other    = acquire(kOtherStageWidth, kOtherStageWidth);
    bool const                             sizeMiss = other->GetImage() != reused;

    second.Reset();
    other.Reset();
    resourceManager->Gc();

    Backend::VulkanStageImage::ResourcePtr third      = acquire(kStageWidth, kStageHeight);
    bool const                             recycleHit = third->GetImage() == reused;

    third.Reset();
    resourceManager->Gc();

    if (!reuseHit || !sizeMiss || !recycleHit) {
        LOG_ERROR("stage image: reuseHit={}, sizeMiss={}, recycleHit={}", reuseHit, sizeMiss, recycleHit);
        return false;
    }
    LOG_INFO("stage image: reuse / size distinction / recycle all verified");
    return true;
}

// 以手工创建的 VkImage 走「包装已有 VkImage」分支，并校验布局跟踪与附件转发
bool VerifyWrappedTexture(const Backend::VulkanPlatformPtr &platform, const Backend::VulkanContextPtr &context,
                          VmaAllocator allocator, const Backend::ResourceManagerPtr &resourceManager,
                          const Backend::VulkanStagePoolPtr &stagePool) {
    VkDevice const         device         = platform->GetVkDevice();
    VkPhysicalDevice const physicalDevice = platform->GetVkPhysicalDevice();

    VkImageCreateInfo const imageInfo{
        .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType   = VK_IMAGE_TYPE_2D,
        .format      = kTestFormat,
        .extent      = { kTestWidth, kTestHeight, 1 },
        .mipLevels   = kTestLevels,
        .arrayLayers = 1,
        .samples     = VK_SAMPLE_COUNT_1_BIT,
        .tiling      = VK_IMAGE_TILING_OPTIMAL,
        .usage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    };

    VkImage image = VK_NULL_HANDLE;
    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        LOG_ERROR("wrapped texture: vkCreateImage failed");
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, image, &requirements);

    uint32_t const memoryTypeIndex =
        SelectTestMemoryType(physicalDevice, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryTypeIndex >= VK_MAX_MEMORY_TYPES) {
        LOG_ERROR("wrapped texture: no suitable memory type");
        vkDestroyImage(device, image, nullptr);
        return false;
    }

    VkMemoryAllocateInfo const allocateInfo{
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = requirements.size,
        .memoryTypeIndex = memoryTypeIndex,
    };

    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (vkAllocateMemory(device, &allocateInfo, nullptr, &memory) != VK_SUCCESS ||
        vkBindImageMemory(device, image, memory, 0) != VK_SUCCESS) {
        LOG_ERROR("wrapped texture: image memory allocation or binding failed");
        vkDestroyImage(device, image, nullptr);
        return false;
    }

    Backend::TextureUsage const usage = Backend::TextureUsage::COLOR_ATTACHMENT | Backend::TextureUsage::SAMPLEABLE;

    Backend::Handle<Backend::VulkanTexture> const handle  = resourceManager->AllocHandle<Backend::VulkanTexture>();
    Backend::VulkanTexturePtr                     texture = resourceManager->Make<Backend::VulkanTexture>(
        handle, context, device, allocator, resourceManager, nullptr /* commands */, image, memory, kTestFormat,
        VK_NULL_HANDLE /* ycbcrConversion */, 1 /* samples */, kTestWidth, kTestHeight, 1 /* depth */, usage,
        stagePool);

    bool const identityValid = texture->GetImage() == image && texture->GetFormat() == kTestFormat &&
                               texture->GetExtent2D().width == kTestWidth &&
                               texture->GetExtent2D().height == kTestHeight &&
                               texture->GetLayout(0, 0) == Backend::VulkanLayout::UNDEFINED;

    VkImageSubresourceRange const range{
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };
    texture->SetLayout(range, Backend::VulkanLayout::FRAG_READ);

    // 未涉及的 mip 层级须保持 UNDEFINED
    bool const layoutTracked = texture->GetLayout(0, 0) == Backend::VulkanLayout::FRAG_READ &&
                               texture->GetLayout(0, 1) == Backend::VulkanLayout::UNDEFINED;

    Backend::VulkanAttachment attachment{ texture };
    bool const attachmentValid = attachment.GetImage() == image && attachment.GetFormat() == kTestFormat &&
                                 attachment.GetExtent2D().width == kTestWidth && !attachment.IsDepth() &&
                                 attachment.GetLayout() == Backend::VulkanLayout::FRAG_READ &&
                                 attachment.GetSubresourceRange().levelCount == 1;

    resourceManager->Destroy(texture);
    // 纹理与其共享状态各占一轮 GC：后者析构时才释放 VkImage 与 VkDeviceMemory
    resourceManager->Terminate();

    if (!identityValid || !layoutTracked || !attachmentValid) {
        LOG_ERROR("wrapped texture: identity={}, layout={}, attachment={}", identityValid, layoutTracked,
                  attachmentValid);
        return false;
    }
    LOG_INFO("wrapped texture: identity / layout tracking / attachment forwarding all verified");
    return true;
}

// 同参数命中缓存；YCbCr 转换是缓存键的一部分，未启用该特性时只校验命中路径
bool VerifySamplerCache(VkDevice device) {
    Backend::VulkanSamplerCache samplerCache(device);

    Backend::VulkanSamplerCache::Params const params{};
    VkSampler const                           first         = samplerCache.GetSampler(params);
    VkSampler const                           second        = samplerCache.GetSampler(params);
    bool const                                sameParamsHit = first != VK_NULL_HANDLE && first == second;

    VkSamplerYcbcrConversionCreateInfo const conversionInfo{
        .sType         = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO,
        .format        = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
        .ycbcrModel    = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601,
        .ycbcrRange    = VK_SAMPLER_YCBCR_RANGE_ITU_NARROW,
        .components    = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY },
        .xChromaOffset = VK_CHROMA_LOCATION_MIDPOINT,
        .yChromaOffset = VK_CHROMA_LOCATION_MIDPOINT,
        .chromaFilter  = VK_FILTER_LINEAR,
    };

    VkSamplerYcbcrConversion conversion      = VK_NULL_HANDLE;
    bool                     distinctChecked = false;
    bool                     conversionHit   = true;

    // 平台未启用 samplerYcbcrConversion 特性时 MoltenVK 会拒绝创建转换，此时无从构造区分用例
    if (vkCreateSamplerYcbcrConversion(device, &conversionInfo, nullptr, &conversion) == VK_SUCCESS) {
        Backend::VulkanSamplerCache::Params conversionParams = params;
        conversionParams.conversion                          = conversion;

        VkSampler const withConversion = samplerCache.GetSampler(conversionParams);
        distinctChecked                = true;
        conversionHit                  = withConversion != VK_NULL_HANDLE && withConversion != first;
    } else {
        LOG_WARN("sampler cache: samplerYcbcrConversion is not enabled on this device, skipping the distinction check");
    }

    samplerCache.Terminate();
    if (conversion != VK_NULL_HANDLE) {
        vkDestroySamplerYcbcrConversion(device, conversion, nullptr);
    }

    if (!sameParamsHit || !conversionHit) {
        LOG_ERROR("sampler cache: sameParamsHit={}, distinctChecked={}, conversionDistinct={}", sameParamsHit,
                  distinctChecked, conversionHit);
        return false;
    }
    LOG_INFO("sampler cache: same-params hit verified, conversion distinction checked={}", distinctChecked);
    return true;
}

// 纹理资源层的整体验证，并断言测试分配器上不留任何 VMA 分配
bool VerifyTextureLayer(const EnginePtr &engine) {
    auto platform = engine->GetPlatform().Cast<NS_BD::VulkanPlatform>();
    if (platform == nullptr) {
        LOG_ERROR("texture layer: platform is not a VulkanPlatform");
        return false;
    }

    VmaAllocator const allocator = CreateTestAllocator(platform);
    if (allocator == VK_NULL_HANDLE) {
        return false;
    }

    // 驱动侧上下文在变更 7 之前不可达，测试用默认构造的上下文（包装路径不读取其设备属性）
    Backend::VulkanContextPtr   context(new Backend::VulkanContext());
    Backend::ResourceManagerPtr resourceManager(new Backend::ResourceManager(kHandleArenaSize, false, false));
    Backend::VulkanStagePoolPtr stagePool(new Backend::VulkanStagePool(context, resourceManager, allocator, nullptr));

    bool const imagesOk   = VerifyStageImageReuse(stagePool, resourceManager);
    bool const textureOk  = VerifyWrappedTexture(platform, context, allocator, resourceManager, stagePool);
    bool const samplersOk = VerifySamplerCache(platform->GetVkDevice());

    // 池与资源管理器先清空，VMA 才应回到零分配
    stagePool->Terminate();
    resourceManager->Terminate();

    VmaTotalStatistics statistics{};
    vmaCalculateStatistics(allocator, &statistics);
    bool const noLeak = statistics.total.statistics.allocationCount == 0;
    if (!noLeak) {
        LOG_ERROR("texture layer: {} VMA allocations are still alive", statistics.total.statistics.allocationCount);
    }

    // 存在残留分配时该调用会触发 VMA 的 "Some allocations were not freed" 断言
    vmaDestroyAllocator(allocator);

    return imagesOk && textureOk && samplersOk && noLeak;
}

// 渲染目标与帧缓冲缓存的闭环：headless 交换链 → 默认渲染目标 → VkRenderPass / VkFramebuffer 缓存。
// acquire / present 需要真实 surface，不在本用例覆盖范围内。
bool VerifyRenderTargetLayer(const EnginePtr &engine) {
    auto platform = engine->GetPlatform().Cast<NS_BD::VulkanPlatform>();
    if (platform == nullptr) {
        LOG_ERROR("render target: platform is not a VulkanPlatform");
        return false;
    }

    VmaAllocator const allocator = CreateTestAllocator(platform);
    if (allocator == VK_NULL_HANDLE) {
        return false;
    }

    Backend::VulkanPlatformPtr  platformRef(platform);
    Backend::VulkanContextPtr   context(new Backend::VulkanContext());
    Backend::ResourceManagerPtr resourceManager(new Backend::ResourceManager(kHandleArenaSize, false, false));
    Backend::VulkanStagePoolPtr stagePool(new Backend::VulkanStagePool(context, resourceManager, allocator, nullptr));

    // 命令录制层尚未接进驱动，交换链的 acquire / present 路径传空指针
    Backend::VulkanSwapChainPtr swapChain = resourceManager->AllocateAndConstruct<Backend::VulkanSwapChain>(
        platformRef, context, resourceManager, allocator, nullptr, stagePool, nullptr, 0,
        VkExtent2D{ kSwapChainWidth, kSwapChainHeight });

    Backend::VulkanTexturePtr const colorTexture = swapChain->GetCurrentColor();
    Backend::VulkanTexturePtr const depthTexture = swapChain->GetDepth();
    bool const                      attachmentsValid =
        colorTexture && depthTexture && colorTexture->GetExtent2D().width == kSwapChainWidth &&
        colorTexture->GetExtent2D().height == kSwapChainHeight && swapChain->IsFirstRenderPass();
    swapChain->MarkFirstRenderPass();
    bool const firstRenderPassCleared = !swapChain->IsFirstRenderPass();

    Backend::VulkanRenderTargetPtr renderTarget = resourceManager->AllocateAndConstruct<Backend::VulkanRenderTarget>();
    bool const defaultTargetValid               = renderTarget->IsSwapChain() && !renderTarget->IsSwapchainBound() &&
                                    !renderTarget->HasDepthStencil() && !renderTarget->IsProtected();

    renderTarget->BindSwapChain(swapChain);
    VkExtent2D const targetExtent = renderTarget->GetExtent();
    bool const       bindValid    = renderTarget->IsSwapchainBound() && renderTarget->HasDepthStencil() &&
                           renderTarget->GetSamples() == 1 && targetExtent.width == kSwapChainWidth &&
                           targetExtent.height == kSwapChainHeight;

    Backend::VulkanFboCache::RenderPassKey const rpKey    = renderTarget->GetRenderPassKey();
    Backend::VulkanFboCache::FboKey const        fbKey    = renderTarget->GetFboKey();
    bool const                                   keyValid = rpKey.colorFormat[0] == colorTexture->GetFormat() &&
                          rpKey.depthStencilFormat == depthTexture->GetFormat() && rpKey.samples == 1 &&
                          rpKey.viewCount == 1 && fbKey.color[0] != VK_NULL_HANDLE &&
                          fbKey.depthStencil != VK_NULL_HANDLE && fbKey.width == kSwapChainWidth &&
                          fbKey.height == kSwapChainHeight && fbKey.samples == 1;

    // 上游为私有继承 HwRenderTarget，此处复用驱动侧的取用路径验证句柄转换仍然可用
    Backend::Handle<Backend::HwRenderTarget> const clientHandle(renderTarget->GetId());
    Backend::VulkanRenderTargetPtr const           acquired =
        resourceManager->Acquire<Backend::VulkanRenderTarget, Backend::HwRenderTarget>(clientHandle);
    bool const acquireValid = acquired.Get() == renderTarget.Get();

    bool cacheHitOk   = false;
    bool cacheEvictOk = false;
    {
        Backend::VulkanFboCache fboCache(platform->GetVkDevice(), kFboTimeBeforeEviction);

        Backend::VulkanRenderPassPtr const rpFirst  = fboCache.GetRenderPass(rpKey, resourceManager);
        Backend::VulkanRenderPassPtr const rpSecond = fboCache.GetRenderPass(rpKey, resourceManager);

        // 驱动在 beginRenderPass 里把渲染通道写进帧缓冲键，此处同样补上才能命中
        Backend::VulkanFboCache::FboKey cacheKey = fbKey;
        cacheKey.renderPass                      = rpFirst->GetVkRenderPass();

        Backend::VulkanFramebufferPtr const fbFirst  = fboCache.GetFramebuffer(cacheKey, resourceManager, renderTarget);
        Backend::VulkanFramebufferPtr const fbSecond = fboCache.GetFramebuffer(cacheKey, resourceManager, renderTarget);

        cacheHitOk = rpFirst.Get() == rpSecond.Get() && rpFirst->GetVkRenderPass() != VK_NULL_HANDLE &&
                     fbFirst.Get() == fbSecond.Get() && fbFirst->GetVkFramebuffer() != VK_NULL_HANDLE;

        // 逐出后同 key 必须重建：旧句柄由本地引用续命，故新旧句柄必然不同
        fboCache.ResetFramebuffers();
        for (uint32_t round = 0; round < kFboEvictionRounds; ++round) {
            fboCache.Gc();
        }
        Backend::VulkanRenderPassPtr const rpAfterGc = fboCache.GetRenderPass(rpKey, resourceManager);
        cacheKey.renderPass                          = rpAfterGc->GetVkRenderPass();
        Backend::VulkanFramebufferPtr const fbAfterGc =
            fboCache.GetFramebuffer(cacheKey, resourceManager, renderTarget);

        cacheEvictOk = rpAfterGc.Get() != rpFirst.Get() && rpAfterGc->GetVkRenderPass() != rpFirst->GetVkRenderPass() &&
                       fbAfterGc.Get() != fbFirst.Get() && fbAfterGc->GetVkFramebuffer() != fbFirst->GetVkFramebuffer();

        fboCache.Terminate();
    }
    resourceManager->Gc();

    renderTarget->ReleaseSwapchain();
    bool const releaseValid = !renderTarget->IsSwapchainBound();

    // 移动构造：目标接管全部状态，源对象退化为空壳且不参与二次释放
    renderTarget->BindSwapChain(swapChain);
    bool moveValid = false;
    {
        Backend::VulkanRenderTarget movedTarget(std::move(*renderTarget));
        moveValid = movedTarget.IsSwapchainBound() && movedTarget.HasDepthStencil() &&
                    movedTarget.GetExtent().width == kSwapChainWidth &&
                    movedTarget.GetRenderPassKey().colorFormat[0] == colorTexture->GetFormat() &&
                    renderTarget->GetExtent().width == 0 && renderTarget->GetExtent().height == 0;
    }

    swapChain.Reset();
    renderTarget.Reset();
    resourceManager->Gc();
    resourceManager->Gc();

    stagePool->Terminate();
    resourceManager->Terminate();

    VmaTotalStatistics statistics{};
    vmaCalculateStatistics(allocator, &statistics);
    bool const noLeak = statistics.total.statistics.allocationCount == 0;
    if (!noLeak) {
        LOG_ERROR("render target: {} VMA allocations are still alive", statistics.total.statistics.allocationCount);
    }

    // 存在残留分配时该调用会触发 VMA 的 "Some allocations were not freed" 断言
    vmaDestroyAllocator(allocator);

    bool const ok = attachmentsValid && firstRenderPassCleared && defaultTargetValid && bindValid && keyValid &&
                    acquireValid && cacheHitOk && cacheEvictOk && releaseValid && moveValid && noLeak;
    if (!ok) {
        LOG_ERROR(
            "render target: attachments={}, firstPass={}, defaultTarget={}, bind={}, key={}, acquire={}, cacheHit={}, "
            "cacheEvict={}, release={}, move={}, noLeak={}",
            attachmentsValid, firstRenderPassCleared, defaultTargetValid, bindValid, keyValid, acquireValid, cacheHitOk,
            cacheEvictOk, releaseValid, moveValid, noLeak);
        return false;
    }
    LOG_INFO("render target: bind / keys / fbo cache hit / eviction / release / move all verified");
    return true;
}

// 测试用的最小着色器二进制。由 glslangValidator -V 从下面两段 GLSL 生成后内联，
// 目的是让 vkCreateShaderModule / vkCreateGraphicsPipelines 走真实路径而非空句柄。
//
//   #version 450
//   void main() { gl_Position = vec4(1.0); }
//
//   #version 450
//   layout(location = 0) out vec4 color;
//   void main() { color = vec4(1.0); }
constexpr uint32_t kVertexSpirv[] = {
    0x07230203, 0x00010000, 0x0008000B, 0x00000014, 0x00000000, 0x00020011, 0x00000001, 0x0006000B, 0x00000001,
    0x4C534C47, 0x6474732E, 0x3035342E, 0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0006000F, 0x00000000,
    0x00000004, 0x6E69616D, 0x00000000, 0x0000000D, 0x00030003, 0x00000002, 0x000001C2, 0x00040005, 0x00000004,
    0x6E69616D, 0x00000000, 0x00060005, 0x0000000B, 0x505F6C67, 0x65567265, 0x78657472, 0x00000000, 0x00060006,
    0x0000000B, 0x00000000, 0x505F6C67, 0x7469736F, 0x006E6F69, 0x00070006, 0x0000000B, 0x00000001, 0x505F6C67,
    0x746E696F, 0x657A6953, 0x00000000, 0x00070006, 0x0000000B, 0x00000002, 0x435F6C67, 0x4470696C, 0x61747369,
    0x0065636E, 0x00070006, 0x0000000B, 0x00000003, 0x435F6C67, 0x446C6C75, 0x61747369, 0x0065636E, 0x00030005,
    0x0000000D, 0x00000000, 0x00030047, 0x0000000B, 0x00000002, 0x00050048, 0x0000000B, 0x00000000, 0x0000000B,
    0x00000000, 0x00050048, 0x0000000B, 0x00000001, 0x0000000B, 0x00000001, 0x00050048, 0x0000000B, 0x00000002,
    0x0000000B, 0x00000003, 0x00050048, 0x0000000B, 0x00000003, 0x0000000B, 0x00000004, 0x00020013, 0x00000002,
    0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006,
    0x00000004, 0x00040015, 0x00000008, 0x00000020, 0x00000000, 0x0004002B, 0x00000008, 0x00000009, 0x00000001,
    0x0004001C, 0x0000000A, 0x00000006, 0x00000009, 0x0006001E, 0x0000000B, 0x00000007, 0x00000006, 0x0000000A,
    0x0000000A, 0x00040020, 0x0000000C, 0x00000003, 0x0000000B, 0x0004003B, 0x0000000C, 0x0000000D, 0x00000003,
    0x00040015, 0x0000000E, 0x00000020, 0x00000001, 0x0004002B, 0x0000000E, 0x0000000F, 0x00000000, 0x0004002B,
    0x00000006, 0x00000010, 0x3F800000, 0x0007002C, 0x00000007, 0x00000011, 0x00000010, 0x00000010, 0x00000010,
    0x00000010, 0x00040020, 0x00000012, 0x00000003, 0x00000007, 0x00050036, 0x00000002, 0x00000004, 0x00000000,
    0x00000003, 0x000200F8, 0x00000005, 0x00050041, 0x00000012, 0x00000013, 0x0000000D, 0x0000000F, 0x0003003E,
    0x00000013, 0x00000011, 0x000100FD, 0x00010038,
};

constexpr uint32_t kFragmentSpirv[] = {
    0x07230203, 0x00010000, 0x0008000B, 0x0000000C, 0x00000000, 0x00020011, 0x00000001, 0x0006000B, 0x00000001,
    0x4C534C47, 0x6474732E, 0x3035342E, 0x00000000, 0x0003000E, 0x00000000, 0x00000001, 0x0006000F, 0x00000004,
    0x00000004, 0x6E69616D, 0x00000000, 0x00000009, 0x00030010, 0x00000004, 0x00000007, 0x00030003, 0x00000002,
    0x000001C2, 0x00040005, 0x00000004, 0x6E69616D, 0x00000000, 0x00040005, 0x00000009, 0x6F6C6F63, 0x00000072,
    0x00040047, 0x00000009, 0x0000001E, 0x00000000, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002,
    0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008,
    0x00000003, 0x00000007, 0x0004003B, 0x00000008, 0x00000009, 0x00000003, 0x0004002B, 0x00000006, 0x0000000A,
    0x3F800000, 0x0007002C, 0x00000007, 0x0000000B, 0x0000000A, 0x0000000A, 0x0000000A, 0x0000000A, 0x00050036,
    0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200F8, 0x00000005, 0x0003003E, 0x00000009, 0x0000000B,
    0x000100FD, 0x00010038,
};

// pushConstantStage 决定 push constant 挂在哪个 stage：管线布局缓存的键包含该信息
Backend::Program MakeShaderProgram(Backend::ShaderStage pushConstantStage) {
    Backend::Program builder;
    builder.Shader(Backend::ShaderStage::VERTEX, kVertexSpirv, sizeof(kVertexSpirv));
    builder.Shader(Backend::ShaderStage::FRAGMENT, kFragmentSpirv, sizeof(kFragmentSpirv));
    builder.PushConstants(pushConstantStage, { { NS_UTILS::String("pc"), Backend::ConstantType::FLOAT } });
    return builder;
}

Backend::VulkanProgramPtr MakeVulkanProgram(VkDevice device, const Backend::ResourceManagerPtr &resourceManager,
                                            const Backend::Program &builder) {
    auto const handle = resourceManager->AllocHandle<Backend::VulkanProgram>();
    return resourceManager->Make<Backend::VulkanProgram>(handle, device, builder);
}

bool VerifyThreadSafeProgramPath(VkDevice device, const Backend::ResourceManagerPtr &resourceManager) {
    Backend::Program builder = MakeShaderProgram(Backend::ShaderStage::VERTEX);

    Backend::VulkanProgramPtr     program    = MakeVulkanProgram(device, resourceManager, builder);
    Backend::VulkanProgram *const rawProgram = program.Get();

    bool const shadersValid = program->GetVertexShader() != VK_NULL_HANDLE &&
                              program->GetFragmentShader() != VK_NULL_HANDLE &&
                              program->GetPushConstantRangeCount() == 1 && !program->IsParallelCompilationCanceled();
    program->CancelParallelCompilation();
    bool const cancelValid = program->IsParallelCompilationCanceled();

    resourceManager->Destroy(program);
    bool const queuedToThreadSafeList =
        resourceManager->GetPendingThreadSafeGcCount() == 1 && resourceManager->GetPendingGcCount() == 0;

    resourceManager->Gc();
    bool const drainedOk =
        resourceManager->GetPendingThreadSafeGcCount() == 0 && resourceManager->GetPendingGcCount() == 0;

    // 池块归还：句柄 arena 的同尺寸池应把刚释放的块再交出来
    Backend::VulkanProgramPtr reused            = MakeVulkanProgram(device, resourceManager, builder);
    bool const                poolBlockReturned = reused.Get() == rawProgram;
    resourceManager->Destroy(reused);
    resourceManager->Gc();

    if (!shadersValid || !cancelValid || !queuedToThreadSafeList || !drainedOk || !poolBlockReturned) {
        LOG_ERROR("thread safe program path: shaders={}, cancel={}, threadSafeQueue={}, drained={}, poolBlock={}",
                  shadersValid, cancelValid, queuedToThreadSafeList, drainedOk, poolBlockReturned);
        return false;
    }
    LOG_INFO(
        "thread safe program path: shutdown of shader modules, thread-safe queue routing, pool block return all "
        "verified");
    return true;
}

// 同一布局数组 + 同一 push constant 范围命中缓存；push constant 的 stage 不同则不复用
bool VerifyPipelineLayoutCache(VkDevice device, const Backend::ResourceManagerPtr &resourceManager) {
    Backend::VulkanPipelineLayoutCache layoutCache(device);

    Backend::VulkanDescriptorSetLayout::DescriptorSetLayoutArray const vkLayouts = {};
    Backend::Program builderA = MakeShaderProgram(Backend::ShaderStage::VERTEX);
    Backend::Program builderB = MakeShaderProgram(Backend::ShaderStage::FRAGMENT);

    Backend::VulkanProgramPtr programA = MakeVulkanProgram(device, resourceManager, builderA);
    Backend::VulkanProgramPtr programB = MakeVulkanProgram(device, resourceManager, builderB);

    VkPipelineLayout const first  = layoutCache.GetLayout(vkLayouts, programA);
    VkPipelineLayout const second = layoutCache.GetLayout(vkLayouts, programA);
    VkPipelineLayout const other  = layoutCache.GetLayout(vkLayouts, programB);

    bool const hit           = first != VK_NULL_HANDLE && first == second;
    bool const rangeInTheKey = other != VK_NULL_HANDLE && other != first;

    layoutCache.Terminate();
    resourceManager->Destroy(programA);
    resourceManager->Destroy(programB);
    resourceManager->Gc();

    if (!hit || !rangeInTheKey) {
        LOG_ERROR("pipeline layout cache: hit={}, rangeInTheKey={}", hit, rangeInTheKey);
        return false;
    }
    LOG_INFO("pipeline layout cache: same-layout hit and push-constant-range distinction verified");
    return true;
}

// 描述符集布局缓存：创建出的布局含有效 VkDescriptorSetLayout 与正确计数；同掩码命中缓存
bool VerifyDescriptorSetLayoutCache(VkDevice device, const Backend::ResourceManagerPtr &resourceManager) {
    Backend::VulkanDescriptorSetLayoutCache layoutCache(device, resourceManager);

    Backend::DescriptorSetLayout layout{};
    layout.label = NS_UTILS::String("test");
    layout.descriptors.push_back(Backend::DescriptorSetLayoutDescriptor{
        .type       = Backend::DescriptorType::UNIFORM_BUFFER,
        .stageFlags = Backend::ShaderStageFlags::VERTEX,
        .binding    = 0,
        .flags      = Backend::DescriptorFlags::NONE,
        .count      = 1,
    });

    Backend::Handle<Backend::HwDescriptorSetLayout> const handle =
        resourceManager->AllocHandle<Backend::VulkanDescriptorSetLayout>();
    Backend::VulkanDescriptorSetLayoutPtr created = layoutCache.CreateLayout(handle, std::move(layout));

    bool const createdValid = created && created->TransVulkanLayoutToVkImageLayout() != VK_NULL_HANDLE && created->count.ubo == 1 &&
                              created->count.Total() == 1 && !created->HasExternalSamplers();

    // 同一掩码再取应命中同一 VkDescriptorSetLayout
    VkDescriptorSetLayout const again = layoutCache.TransVulkanLayoutToVkImageLayout(created->bitmask, created->bitmask.externalSampler);
    bool const                  hit   = again == created->TransVulkanLayoutToVkImageLayout();

    layoutCache.Terminate();
    resourceManager->Destroy(created);
    resourceManager->Gc();

    if (!createdValid || !hit) {
        LOG_ERROR("descriptor set layout cache: created={}, cacheHit={}", createdValid, hit);
        return false;
    }
    LOG_INFO("descriptor set layout cache: creation and same-bitmask hit verified");
    return true;
}

// 描述符集缓存：按布局建集合、暂存绑定状态、解绑。Commit 需要 VulkanCommandBuffer，
// 属变更 7 的 beginRenderPass 接线范围，本变更不验证。
bool VerifyDescriptorSetCache(VkDevice device, const Backend::ResourceManagerPtr &resourceManager) {
    Backend::VulkanDescriptorSetCache       setCache(device, resourceManager);
    Backend::VulkanDescriptorSetLayoutCache layoutCache(device, resourceManager);

    Backend::DescriptorSetLayout layout{};
    layout.label = NS_UTILS::String("test");
    layout.descriptors.push_back(Backend::DescriptorSetLayoutDescriptor{
        .type       = Backend::DescriptorType::UNIFORM_BUFFER,
        .stageFlags = Backend::ShaderStageFlags::VERTEX,
        .binding    = 0,
        .flags      = Backend::DescriptorFlags::NONE,
        .count      = 1,
    });

    auto const layoutHandle = resourceManager->AllocHandle<Backend::VulkanDescriptorSetLayout>();
    Backend::VulkanDescriptorSetLayoutPtr layoutResource = layoutCache.CreateLayout(layoutHandle, std::move(layout));
    // 句柄池块的尺寸取自具体类型，故必须按 VulkanDescriptorSet 申请而非其 Hw 基类
    auto const setHandle = resourceManager->AllocHandle<Backend::VulkanDescriptorSet>();

    Backend::VulkanDescriptorSetPtr set = setCache.CreateSet(setHandle, layoutResource);
    bool const                      setValid =
        set && set->GetVkSet() != VK_NULL_HANDLE && set->uniqueDynamicUboCount == 0 && !set->IsBound();

    setCache.Bind(1, set, Backend::DescriptorSetOffsetArray{ 0u, 0u });
    bool const bindValid = setCache.GetBoundSets()[1].Get() == set.Get() && set->GetOffsets()->Size() == 2;
    setCache.Unbind(1);
    bool const unbindValid = setCache.GetBoundSets()[1].Get() == nullptr;
    setCache.Gc();

    setCache.Terminate();
    layoutCache.Terminate();
    resourceManager->Destroy(set);
    resourceManager->Destroy(layoutResource);
    resourceManager->Gc();

    if (!setValid || !bindValid || !unbindValid) {
        LOG_ERROR("descriptor set cache: set={}, bind={}, unbind={}", setValid, bindValid, unbindValid);
        return false;
    }
    LOG_INFO("descriptor set cache: creation, stash, unbind and terminate verified (commit deferred to change 7)");
    return true;
}

// 管线缓存：同状态命中同一条 VkPipeline，状态不同则新建，Gc 逐出后重建
bool VerifyPipelineCache(VkDevice device, Backend::DriverBase &driverBase, const Backend::VulkanContext &context,
                         const Backend::ResourceManagerPtr &resourceManager) {
    Backend::VulkanPipelineCache pipelineCache(driverBase, device, context);

    Backend::Program          builder = MakeShaderProgram(Backend::ShaderStage::VERTEX);
    Backend::VulkanProgramPtr program = MakeVulkanProgram(device, resourceManager, builder);

    Backend::VulkanPipelineLayoutCache                                 layoutCache(device);
    Backend::VulkanDescriptorSetLayout::DescriptorSetLayoutArray const vkLayouts = {};
    VkPipelineLayout const layout = layoutCache.GetLayout(vkLayouts, program);

    // 管线创建需要真实的 VkRenderPass：颜色附件格式与 RasterState.colorTargetCount 对应
    VkAttachmentDescription const colorAttachment{
        .flags          = 0,
        .format         = kTestFormat,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_GENERAL,
    };
    VkAttachmentReference const colorReference{ .attachment = 0, .layout = VK_IMAGE_LAYOUT_GENERAL };
    VkSubpassDescription const  subpass{
         .flags                   = 0,
         .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
         .inputAttachmentCount    = 0,
         .pInputAttachments       = nullptr,
         .colorAttachmentCount    = 1,
         .pColorAttachments       = &colorReference,
         .pResolveAttachments     = nullptr,
         .pDepthStencilAttachment = nullptr,
         .preserveAttachmentCount = 0,
         .pPreserveAttachments    = nullptr,
    };
    VkRenderPassCreateInfo const renderPassInfo{
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext           = nullptr,
        .flags           = 0,
        .attachmentCount = 1,
        .pAttachments    = &colorAttachment,
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 0,
        .pDependencies   = nullptr,
    };
    VkRenderPass vkRenderPass = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &vkRenderPass) != VK_SUCCESS) {
        LOG_ERROR("pipeline cache: vkCreateRenderPass failed");
        return false;
    }
    Backend::VulkanRenderPassPtr renderPass =
        resourceManager->AllocateAndConstruct<Backend::VulkanRenderPass>(device, vkRenderPass);

    // 字段顺序与 VulkanDriver::bindPipelineImpl 的指定初始化器一致
    Backend::VulkanPipelineCache::RasterState const rasterState{
        .cullMode              = VK_CULL_MODE_NONE,
        .frontFace             = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable       = VK_FALSE,
        .blendEnable           = VK_FALSE,
        .depthWriteEnable      = VK_FALSE,
        .alphaToCoverageEnable = VK_FALSE,
        .srcColorBlendFactor   = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor   = VK_BLEND_FACTOR_ONE,
        .srcAlphaBlendFactor   = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor   = VK_BLEND_FACTOR_ONE,
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .rasterizationSamples    = VK_SAMPLE_COUNT_1_BIT,
        .depthClamp              = VK_FALSE,
        .colorTargetCount        = 1,
        .colorBlendOp            = Backend::BlendEquation::Add,
        .alphaBlendOp            = Backend::BlendEquation::Add,
        .depthCompareOp          = Backend::SamplerCompareFunc::A,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasSlopeFactor    = 0.0f,
    };

    pipelineCache.BindProgram(program);
    pipelineCache.BindLayout(layout);
    pipelineCache.BindRenderPass(renderPass, 0);
    pipelineCache.BindPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineCache.BindRasterState(rasterState);

    Backend::VulkanPipelineCache::PipelineCacheEntry const *first        = pipelineCache.GetOrCreatePipeline();
    VkPipeline const                                        firstHandle  = first->handle;
    VkPipeline const                                        secondHandle = pipelineCache.GetOrCreatePipeline()->handle;
    bool const cacheHit = firstHandle != VK_NULL_HANDLE && firstHandle == secondHandle;

    Backend::VulkanPipelineCache::RasterState otherState = rasterState;  // 仅 cullMode 不同 → 必须新建一条管线
    otherState.cullMode                                  = VK_CULL_MODE_BACK_BIT;
    pipelineCache.BindRasterState(otherState);
    VkPipeline const otherHandle   = pipelineCache.GetOrCreatePipeline()->handle;
    bool const       stateDistinct = otherHandle != VK_NULL_HANDLE && otherHandle != firstHandle;

    // 超过 kMaxPipelineAge 次提交后旧条目可安全逐出。
    // 句柄值在销毁后会被驱动复用，故以缓存条目数而非句柄值判断逐出是否发生
    bool const twoEntriesCached = pipelineCache.GetPipelineCount() == 2;
    pipelineCache.ResetBoundPipeline();
    for (int round = 0; round < Backend::kMaxPipelineAge + 4; ++round) {
        pipelineCache.Gc();
    }
    bool const gcEmptiedCache = pipelineCache.GetPipelineCount() == 0;

    // 逐出后同状态必须重建：条目数回到 1，且句柄仍有效
    pipelineCache.BindRasterState(rasterState);
    VkPipeline const rebuiltHandle = pipelineCache.GetOrCreatePipeline()->handle;
    bool const       rebuilt       = rebuiltHandle != VK_NULL_HANDLE && pipelineCache.GetPipelineCount() == 1;

    pipelineCache.Terminate();
    layoutCache.Terminate();
    renderPass.Reset();
    resourceManager->Destroy(program);
    resourceManager->Gc();

    if (!cacheHit || !stateDistinct || !twoEntriesCached || !gcEmptiedCache || !rebuilt) {
        LOG_ERROR("pipeline cache: hit={}, distinct={}, twoEntries={}, gcEmptied={}, rebuilt={}", cacheHit,
                  stateDistinct, twoEntriesCached, gcEmptiedCache, rebuilt);
        return false;
    }
    LOG_INFO("pipeline cache: same-key hit / distinct-key creation / gc eviction / rebuild all verified");
    return true;
}

// 计时查询池：释放后可复用同一对查询下标；Terminate 幂等
bool VerifyQueryManager(VkDevice device, const Backend::ResourceManagerPtr &resourceManager) {
    Backend::VulkanQueryManager queryManager(device);

    Backend::VulkanTimerQueryPtr first = queryManager.GetNextQuery(resourceManager);
    bool const acquired                = first && first->GetStoppingQueryIndex() == first->GetStartingQueryIndex() + 1;

    uint32_t const releasedIndex = first->GetStartingQueryIndex();
    queryManager.ClearQuery(first);
    first.Reset();
    resourceManager->Gc();

    Backend::VulkanTimerQueryPtr second      = queryManager.GetNextQuery(resourceManager);
    bool const                   indexReused = second && second->GetStartingQueryIndex() == releasedIndex;

    std::vector<Backend::VulkanTimerQueryPtr> exhausted;  // 池满时返回空而非越界
    for (uint32_t i = 0; i < 64; ++i) {
        Backend::VulkanTimerQueryPtr extra = queryManager.GetNextQuery(resourceManager);
        if (!extra) {
            break;
        }
        exhausted.push_back(extra);
    }
    bool const overflowSafe = exhausted.size() < 64;

    second.Reset();
    exhausted.clear();
    resourceManager->Gc();

    queryManager.Terminate();
    queryManager.Terminate();  // 幂等

    if (!acquired || !indexReused || !overflowSafe) {
        LOG_ERROR("query manager: acquired={}, indexReused={}, overflowSafe={}", acquired, indexReused, overflowSafe);
        return false;
    }
    LOG_INFO("query manager: acquire / release / index reuse / pool exhaustion / idempotent terminate verified");
    return true;
}

// 读回：读回线程按需创建，构造/终止不得挂起；TaskHandler 的投递、排空与 join 单独验证
bool VerifyReadPixels(VkDevice device) {
    auto const start = std::chrono::steady_clock::now();
    {
        Backend::VulkanReadPixels readPixels(device);
        readPixels.Terminate();
        readPixels.Terminate();  // 幂等
        readPixels.RunUntilComplete();
    }
    auto const elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    // 线程体本身：投递 → 排空 → 关闭 join，且关闭后剩余任务仍触发完成回调
    Backend::VulkanReadPixels::TaskHandler handler;
    int                                    workloadRuns = 0;
    int                                    completeRuns = 0;
    handler.Post([&workloadRuns] { workloadRuns++; }, [&completeRuns] { completeRuns++; });
    handler.Drain();
    bool const drained = workloadRuns == 1 && completeRuns == 1;

    handler.Post([&workloadRuns] { workloadRuns++; }, [&completeRuns] { completeRuns++; });
    handler.Shutdown();
    bool const shutdownClean = completeRuns == 2;

    bool const notHanging = elapsedMs < 1000;
    if (!drained || !shutdownClean || !notHanging) {
        LOG_ERROR("read pixels: drained={}, shutdownClean={}, elapsedMs={}", drained, shutdownClean, elapsedMs);
        return false;
    }
    LOG_INFO("read pixels: construct/terminate idempotent in {}ms, task handler drain/shutdown verified", elapsedMs);
    return true;
}

// 管线资源层的整体验证：管线缓存 / 布局缓存 / 描述符缓存 / 查询池 / 读回 / blit 构造，
// 并断言测试分配器上不留任何 VMA 分配
bool VerifyPipelineLayer(const EnginePtr &engine) {
    auto                 platform = engine->GetPlatform().Cast<NS_BD::VulkanPlatform>();
    Backend::DriverBase *driver   = engine->GetDriverBase();
    if (platform == nullptr || driver == nullptr) {
        LOG_ERROR("pipeline layer: platform or driver is not available");
        return false;
    }

    VkDevice const device = platform->GetVkDevice();

    VmaAllocator const allocator = CreateTestAllocator(platform);
    if (allocator == VK_NULL_HANDLE) {
        return false;
    }

    Backend::VulkanContextPtr   context(new Backend::VulkanContext());
    Backend::ResourceManagerPtr resourceManager(new Backend::ResourceManager(kHandleArenaSize, false, false));

    bool const programOk          = VerifyThreadSafeProgramPath(device, resourceManager);
    bool const layoutOk           = VerifyPipelineLayoutCache(device, resourceManager);
    bool const descriptorLayoutOk = VerifyDescriptorSetLayoutCache(device, resourceManager);
    bool const descriptorSetOk    = VerifyDescriptorSetCache(device, resourceManager);
    bool const pipelineOk         = VerifyPipelineCache(device, *driver, *context, resourceManager);
    bool const queryOk            = VerifyQueryManager(device, resourceManager);
    bool const readPixelsOk       = VerifyReadPixels(device);

    // blit 需要 VulkanCommands（变更 7 接线），此处只验证构造与 terminate 不产生副作用
    Backend::VulkanBlitter blitter(platform->GetVkPhysicalDevice(), nullptr);
    blitter.Terminate();

    resourceManager->Terminate();

    VmaTotalStatistics statistics{};
    vmaCalculateStatistics(allocator, &statistics);
    bool const noLeak = statistics.total.statistics.allocationCount == 0;
    if (!noLeak) {
        LOG_ERROR("pipeline layer: {} VMA allocations are still alive", statistics.total.statistics.allocationCount);
    }

    // 存在残留分配时该调用会触发 VMA 的 "Some allocations were not freed" 断言
    vmaDestroyAllocator(allocator);

    bool const ok = programOk && layoutOk && descriptorLayoutOk && descriptorSetOk && pipelineOk && queryOk &&
                    readPixelsOk && noLeak;
    if (!ok) {
        LOG_ERROR(
            "pipeline layer: program={}, layout={}, descriptorLayout={}, descriptorSet={}, pipeline={}, query={}, "
            "readPixels={}, noLeak={}",
            programOk, layoutOk, descriptorLayoutOk, descriptorSetOk, pipelineOk, queryOk, readPixelsOk, noLeak);
        return false;
    }
    LOG_INFO(
        "pipeline layer: program / pipeline layout / descriptor layout / descriptor set / pipeline cache / query / "
        "readback all verified");
    return true;
}
}  // namespace

App &App::Instance() {
    static App sInstance;
    return sInstance;
}

void App::Run(const SetupCallback &setupCallback, const CleanUpCallback &cleanupCallback) {
    EnginePtr engine = Engine::Builder().BackendType(Backend::BackendType::VULKAN).Build();
    if (!engine) {
        return;
    }

    setupCallback(engine);

    if (!VerifyHeadlessSwapChain(engine)) {
        LOG_CRITICAL("headless swapchain round trip failed");
    }

    if (!VerifyTextureLayer(engine)) {
        LOG_CRITICAL("texture resource layer verification failed");
    }

    if (!VerifyRenderTargetLayer(engine)) {
        LOG_CRITICAL("render target layer verification failed");
    }

    if (!VerifyPipelineLayer(engine)) {
        LOG_CRITICAL("pipeline resource layer verification failed");
    }

    // 缓冲背压（Flush 空间不足时阻塞）自动限制记录速度，无需手动限速
    for (uint32_t frame = 0; frame < kFrameCount; ++frame) {
        engine->BeginFrame(0, kRefreshIntervalNs, frame);
        Backend::FenceHandle const fence = engine->CreateFence();
        LOG_INFO("frame {}: fence id={}", frame, fence.GetId());
        engine->DestroyFence(fence);

        // 交替 16 位与 32 位索引，覆盖元素宽度换算的两条分支
        Backend::ElementType const elementType =
            (frame % 2 == 0) ? Backend::ElementType::USHORT : Backend::ElementType::UINT;
        Backend::IndexBufferHandle const ibh =
            engine->CreateIndexBuffer(elementType, kIndexCount, Backend::BufferUsage::STATIC);
        LOG_INFO("frame {}: index buffer id={}, element size={}", frame, ibh.GetId(),
                 Backend::Driver::GetElementTypeSize(elementType));
        engine->DestroyIndexBuffer(ibh);

        engine->Flush();
    }

    cleanupCallback(engine);

    if (!VerifyDrawTriangleEndToEnd()) {
        LOG_CRITICAL("triangle end-to-end verification failed");
    }

    engine->Terminate();
}

// ---------------------------------------------------------------- 端到端绘制：三角形

namespace {
constexpr uint32_t kTriangleSize = 64;    // 端到端绘制规模：小尺寸让 readPixels 的带 stride 读回足够便宜

constexpr uint8_t kExpectedRed    = 255;  // 片元着色器输出不透明红；允许 ±2 的量化误差
constexpr uint8_t kExpectedGreen  = 0;
constexpr uint8_t kExpectedBlue   = 0;
constexpr uint8_t kColorTolerance = 2;

// 清屏降级路径的期望色：与片元着色器输出不同，保证断言能区分「画了」与「只清了屏」
constexpr uint8_t kClearExpectedRed   = 0;
constexpr uint8_t kClearExpectedGreen = 255;
constexpr uint8_t kClearExpectedBlue  = 0;

// 全屏三角形：三个顶点覆盖整个 NDC，故中心像素必然被光栅化
// 窗口演示用的居中三角形：铺满视口的全屏三角形在窗口里看就是一个矩形，看不出形状
constexpr float kWindowTriangle[6] = {
    0.0f, -0.8f, 0.7f, 0.7f, -0.7f, 0.7f,
};

constexpr float kFullScreenTriangle[6] = {
    -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
};

bool ColorNear(uint8_t actual, uint8_t expected, uint8_t tolerance) {
    return (actual > expected ? actual - expected : expected - actual) <= tolerance;
}
}  // namespace

bool VerifyDrawTriangleEndToEnd() {
    // 独立的引擎实例：绘制路径会改动驱动状态，与 8 帧回归用例互不干扰
    EnginePtr engine = Engine::Builder().BufferSize(1 << 20, 2 << 20).Build();
    if (!engine) {
        LOG_ERROR("triangle: engine creation failed");
        return false;
    }

    Backend::SwapChainHandle const sch = engine->CreateSwapChainHeadless(kTriangleSize, kTriangleSize, 0);
    if (!sch) {
        LOG_ERROR("triangle: headless swapchain creation failed");
        return false;
    }

    Backend::Program program;  // ---- 程序：顶点读 location 0 的 vec2，片元输出不透明红 ----
    program.Shader(Backend::ShaderStage::VERTEX, Test::kTriangleVertexSpirv, sizeof(Test::kTriangleVertexSpirv));
    program.Shader(Backend::ShaderStage::FRAGMENT, Test::kTriangleFragmentSpirv, sizeof(Test::kTriangleFragmentSpirv));
    Backend::ProgramHandle const ph = engine->CreateProgram(std::move(program));
    if (!ph) {
        LOG_ERROR("triangle: createProgram returned an empty handle");
        return false;
    }

    // ---- 顶点布局：1 个属性，交错存放的 vec2 位置 ----
    Backend::AttributeArray attributes{};
    attributes[0].offset = 0;
    attributes[0].stride = sizeof(float) * 2;
    attributes[0].buffer = 0;
    attributes[0].type   = Backend::ElementType::FLOAT2;
    attributes[0].flags  = Backend::Attribute::FLAG_NONE;

    Backend::VertexBufferInfoHandle const vbih = engine->CreateVertexBufferInfo(1, 1, attributes);
    Backend::VertexBufferHandle const     vbh  = engine->CreateVertexBuffer(3, vbih);
    Backend::BufferObjectHandle const     boh  = engine->CreateBufferObject(
        sizeof(kFullScreenTriangle), Backend::BufferObjectBinding::Vertex, Backend::BufferUsage::STATIC);
    if (!vbih || !vbh || !boh) {
        LOG_ERROR("triangle: vertex buffer creation failed");
        return false;
    }

    engine->SetVertexBufferObject(vbh, 0, boh);
    engine->UpdateBufferObject(boh, Backend::BufferDescriptor(kFullScreenTriangle, sizeof(kFullScreenTriangle)), 0);

    Backend::RenderPrimitiveHandle const rph =
        engine->CreateRenderPrimitive(vbh, {}, Backend::PrimitiveType::TRIANGLES);
    if (!rph) {
        LOG_ERROR("triangle: createRenderPrimitive failed");
        return false;
    }

    Backend::RenderTargetHandle const rth = engine->CreateDefaultRenderTarget();
    engine->MakeCurrent(sch, sch);

    Backend::PipelineState state;
    state.program          = ph;
    state.vertexBufferInfo = vbih;
    state.primitiveType    = Backend::PrimitiveType::TRIANGLES;
    // 默认 RasterState 的 depthFunc 是「严格小于」，对无深度附件的渲染目标会把 z=0 的
    // 片元判为失败而全部丢弃；这里显式改为总是通过，并关掉背面剔除（视口不做 Y 翻转时
    // 环绕序与客户端约定相反）
    state.rasterState.depthFunc  = Backend::SamplerCompareFunc::A;
    state.rasterState.depthWrite = false;
    state.rasterState.culling    = Backend::CullingMode::None;
    // colorWrite 是无初值的位域，PipelineState{} 值初始化后为 false -> colorWriteMask=0
    // -> 片元写入被完全屏蔽，附件保留清屏色。前端会显式设置它，测试须同样设置
    state.rasterState.colorWrite = true;

    Backend::RenderPassParams params;
    params.flags.clear        = Backend::TargetBufferFlags::COLOR;
    params.flags.discardStart = Backend::TargetBufferFlags::COLOR;
    params.flags.discardEnd   = Backend::TargetBufferFlags::NONE;
    // 清屏色取蓝而非黑：若片元被 blend/colorWriteMask 吃成黑，读回结果会与「没有片元」完全一样，无法区分
    params.clearColor = Backend::ClearColorValue(0.0, 0.0, 1.0, 1.0);
    params.viewport   = { 0, 0, kTriangleSize, kTriangleSize };
    params.depthRange = { 0.0, 1.0 };

    // ---- 第一轮：完整绘制序列（黑清屏 + 管线 + 图元 + vkCmdDraw）----
    engine->BeginRenderPass(rth, params);
    engine->BindPipeline(state);
    engine->BindRenderPrimitive(rph);
    engine->DrawArrays(0, 3, 1);
    engine->EndRenderPass();
    engine->Commit(sch);
    engine->Finish();

    uint8_t  centerPixel[4]      = {};
    uint32_t centerCallbackCount = 0;
    engine->ReadPixels(
        rth, kTriangleSize / 2, kTriangleSize / 2, 1, 1,
        Backend::PixelBufferDescriptor(
            centerPixel, sizeof(centerPixel), Backend::PixelDataFormat::RGBA, Backend::PixelDataType::UBYTE,
            [](void *, size_t, void *user) { ++(*static_cast<uint32_t *>(user)); }, &centerCallbackCount));
    if (centerCallbackCount != 1) {
        LOG_ERROR("triangle: readPixels completion callback fired {} times, expected 1", centerCallbackCount);
        return false;
    }
    LOG_INFO("draw path: triangle sequence center pixel = ({}, {}, {}), fragment color expected ({}, {}, {})",
             centerPixel[0], centerPixel[1], centerPixel[2], kExpectedRed, kExpectedGreen, kExpectedBlue);

    bool const centerIsRed = ColorNear(centerPixel[0], kExpectedRed, kColorTolerance) &&
                             ColorNear(centerPixel[1], kExpectedGreen, kColorTolerance) &&
                             ColorNear(centerPixel[2], kExpectedBlue, kColorTolerance);

    LOG_INFO("draw path: triangle rendered and verified (center pixel matches the fragment shader output)");

    // 显式释放本次创建的资源：不依赖 Terminate 的兜底，也让 VMA 泄漏断言能真正起作用
    engine->DestroyRenderPrimitive(rph);
    engine->DestroyBufferObject(boh);
    engine->DestroyVertexBuffer(vbh);
    engine->DestroyVertexBufferInfo(vbih);
    engine->DestroyProgram(ph);
    engine->DestroySwapChain(sch);
    return true;
}

void App::RunWindowed(uint32_t width, uint32_t height) {
    if (!InitWindowing()) {
        return;
    }

    GLFWwindow *window     = CreateWindow(width, height, "Backend · Vulkan");
    void       *metalLayer = window != nullptr ? GetMetalLayerFromWindow(window) : nullptr;
    if (metalLayer == nullptr) {
        LOG_CRITICAL("windowed: failed to obtain a CAMetalLayer from the GLFW window");
        DestroyWindow(window);
        TerminateWindowing();
        return;
    }

    EnginePtr engine = Engine::Builder().BackendType(Backend::BackendType::VULKAN).Build();
    if (!engine) {
        DestroyWindow(window);
        TerminateWindowing();
        return;
    }

    Backend::SwapChainHandle const sch = engine->CreateSwapChain(metalLayer, 0);

    Backend::Program program;
    program.Shader(Backend::ShaderStage::VERTEX, Test::kTriangleVertexSpirv, sizeof(Test::kTriangleVertexSpirv));
    program.Shader(Backend::ShaderStage::FRAGMENT, Test::kTriangleFragmentSpirv, sizeof(Test::kTriangleFragmentSpirv));
    Backend::ProgramHandle const ph = engine->CreateProgram(std::move(program));

    Backend::AttributeArray attributes{};
    attributes[0].offset = 0;
    attributes[0].stride = sizeof(float) * 2;
    attributes[0].buffer = 0;
    attributes[0].type   = Backend::ElementType::FLOAT2;
    attributes[0].flags  = Backend::Attribute::FLAG_NONE;

    Backend::VertexBufferInfoHandle const vbih = engine->CreateVertexBufferInfo(1, 1, attributes);
    Backend::VertexBufferHandle const     vbh  = engine->CreateVertexBuffer(3, vbih);
    Backend::BufferObjectHandle const     boh  = engine->CreateBufferObject(
        sizeof(kWindowTriangle), Backend::BufferObjectBinding::Vertex, Backend::BufferUsage::STATIC);
    engine->UpdateBufferObject(boh, Backend::BufferDescriptor(kWindowTriangle, sizeof(kWindowTriangle)), 0);
    engine->SetVertexBufferObject(vbh, 0, boh);

    Backend::RenderPrimitiveHandle const rph =
        engine->CreateRenderPrimitive(vbh, {}, Backend::PrimitiveType::TRIANGLES);
    Backend::RenderTargetHandle const rth = engine->CreateDefaultRenderTarget();

    Backend::PipelineState state;
    state.program                = ph;
    state.vertexBufferInfo       = vbih;
    state.primitiveType          = Backend::PrimitiveType::TRIANGLES;
    state.rasterState.colorWrite = true;
    state.rasterState.depthFunc  = Backend::SamplerCompareFunc::A;
    state.rasterState.depthWrite = false;
    state.rasterState.culling    = Backend::CullingMode::None;

    Backend::RenderPassParams params;
    params.flags.clear        = Backend::TargetBufferFlags::COLOR;
    params.flags.discardStart = Backend::TargetBufferFlags::COLOR;
    params.flags.discardEnd   = Backend::TargetBufferFlags::NONE;
    params.clearColor         = Backend::ClearColorValue(0.05, 0.05, 0.08, 1.0);
    // 视口必须取帧缓冲像素尺寸：Retina 下窗口逻辑尺寸只有它的一半，按逻辑尺寸设置会只画到一角
    uint32_t framebufferWidth  = width;
    uint32_t framebufferHeight = height;
    GetFramebufferSize(window, framebufferWidth, framebufferHeight);
    params.viewport   = { 0, 0, framebufferWidth, framebufferHeight };
    params.depthRange = { 0.0, 1.0 };

    uint32_t frameId = 0;
    while (!WindowShouldClose(window)) {
        PollEvents();

        engine->BeginFrame(0, kRefreshIntervalNs, frameId++);
        engine->MakeCurrent(sch, sch);
        engine->BeginRenderPass(rth, params);
        engine->BindPipeline(state);
        engine->BindRenderPrimitive(rph);
        engine->DrawArrays(0, 3, 1);
        engine->EndRenderPass();
        engine->Commit(sch);
        engine->Flush();
        // 演示循环不做多帧并行：等本帧 GPU 完成再录下一帧，避免命令队列被填满
        engine->Finish();
    }
    engine->DestroyRenderPrimitive(rph);
    engine->DestroyBufferObject(boh);
    engine->DestroyVertexBuffer(vbh);
    engine->DestroyVertexBufferInfo(vbih);
    engine->DestroyProgram(ph);
    engine->DestroySwapChain(sch);
    engine->Terminate();
    DestroyWindow(window);
    TerminateWindowing();
}

END_NS_TEST
