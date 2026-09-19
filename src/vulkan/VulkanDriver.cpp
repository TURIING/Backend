#include "VulkanDriver.h"

#include "Backend/platform/VulkanPlatform.h"

#include "Utils/Log.h"
#include "Utils/Macro.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>

#include "HwDefine.h"
#include "command/CommandStreamDispatcher.h"
#include "vulkan/VkDef.h"
#include "vulkan/VulkanAsyncHandles.h"
#include "vulkan/VulkanHandle.h"
#include "vulkan/VulkanSwapChain.h"
#include "vulkan/buffer/VulkanBuffer.h"
#include "vulkan/resource/ResourceManager.h"
#include "vulkan/utils/Conversion.h"
#include "vulkan/utils/Image.h"

BEGIN_NS_BACKEND

namespace {

// 句柄 arena 取上游默认值 8MB；0（未配置）会得到空 arena 并在 HandleAllocator 初始化时越界断言
constexpr size_t kMinHandleArenaSize = 8u * 1024u * 1024u;

// 被跳过的帧也会累积命令，故必须有独立于 flush() 的 GC 节奏
constexpr uint8_t kMaxTicksBetweenGc = 3;

VmaAllocator CreateAllocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device) noexcept {
    VmaVulkanFunctions const vulkanFunctions{
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr   = vkGetDeviceProcAddr,
    };

    VmaAllocatorCreateInfo const allocatorInfo{
        // 后端单线程访问 VMA，关掉其内部同步以省开销
        .flags            = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT,
        .physicalDevice   = physicalDevice,
        .device           = device,
        .pVulkanFunctions = &vulkanFunctions,
        .instance         = instance,
    };

    VmaAllocator allocator = VK_NULL_HANDLE;
    if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS) {
        LOG_CRITICAL("VulkanDriver - failed to create the VMA allocator");
    }
    return allocator;
}

CallbackHandler::Callback syncCallbackWrapper = [](void* userData) {
    std::unique_ptr<VulkanSync::CallbackData> cbData(static_cast<VulkanSync::CallbackData*>(userData));
    // 此处假定 sync 尚未销毁：销毁与派发的顺序由调用方保证
    cbData->cb(cbData->sync, cbData->userData);
};

// 外部 YCbCr 格式描述 → 不可变采样器换算参数
inline VulkanYcbcrConversionCache::Params GetYcbcrConversionParams(VulkanPlatform::ExternalYcbcrFormat const& format) {
    return VulkanYcbcrConversionCache::Params{
        .conversion = {
            .ycbcrModel   = VK_UTILS::GetYcbcrModelConversionFilament(format.ycbcrModelConversion),
            .r            = VK_UTILS::GetSwizzleFilament(VK_COMPONENT_SWIZZLE_R, 0),
            .g            = VK_UTILS::GetSwizzleFilament(VK_COMPONENT_SWIZZLE_G, 1),
            .b            = VK_UTILS::GetSwizzleFilament(VK_COMPONENT_SWIZZLE_B, 2),
            .a            = VK_UTILS::GetSwizzleFilament(VK_COMPONENT_SWIZZLE_A, 3),
            .ycbcrRange   = VK_UTILS::GetYcbcrRangeFilament(format.ycbcrRange),
            .xChromaOffset = VK_UTILS::GetChromaLocationFilament(VK_CHROMA_LOCATION_MIDPOINT),
            .yChromaOffset = VK_UTILS::GetChromaLocationFilament(VK_CHROMA_LOCATION_MIDPOINT),
            .chromaFilter  = SamplerMagFilter::Nearest,
        },
        .format         = VK_FORMAT_UNDEFINED,
        .externalFormat = format.externalFormat,
    };
}

}  // namespace

VulkanDriver::VulkanDriver(VulkanPlatform* platform, const VulkanContextPtr& context, const DriverConfig& config)
    : DriverBase(config),
      mPlatform(VulkanPlatformPtr(platform)),
      m_resMgr(new ResourceManager(config.handleArenaSize, config.disableHandleUseAfterFreeCheck, config.disableHeapHandleTags)),
      // 默认渲染目标先于 createDefaultRenderTarget() 创建，届时把内容换过去；
      // 这样 createDefaultRenderTarget() 与 makeCurrent() 之间就没有调用顺序约束
      mDefaultRenderTarget(m_resMgr->AllocateAndConstruct<VulkanRenderTarget>()),
      m_allocator(CreateAllocator(platform->GetVkInstance(), platform->GetVkPhysicalDevice(), platform->GetVkDevice())),
      m_context(context),
      m_semaphoreManager(new VulkanSemaphoreManager(platform->GetVkDevice(), m_resMgr)),
      m_commands(platform->GetVkDevice(), platform->GetVkGraphicsQueue(), platform->GetGraphicsQueueFamilyIndex(), m_context, m_semaphoreManager),
      m_pipelineLayoutCache(platform->GetVkDevice()),
      m_pipelineCache(*this, platform->GetVkDevice(), *m_context),
      m_stagePool(new VulkanStagePool(m_context, m_resMgr, m_allocator, &m_commands)),
      m_bufferCache(new VulkanBufferCache(m_context, m_resMgr, m_allocator)),
      m_framebufferCache(platform->GetVkDevice(), platform->GetCustomization().timeBeforeEvictionFbo),
      m_ycbcrConversionCache(platform->GetVkDevice()),
      m_samplerCache(platform->GetVkDevice()),
      m_blitter(platform->GetVkPhysicalDevice(), &m_commands),
      m_readPixels(platform->GetVkDevice()),
      m_descriptorSetLayoutCache(platform->GetVkDevice(), m_resMgr),
      m_descriptorSetCache(platform->GetVkDevice(), m_resMgr),
      m_queryManager(platform->GetVkDevice()),
      mIsSRGBSwapChainSupported(platform->GetCustomization().isSRGBSwapChainSupported),
      mStereoscopicType(config.stereoscopicType),
      mStereoscopicEyeCount(config.stereoscopicEyeCount),
      mAsynchronousMode(config.asynchronousMode) {}

VulkanDriver::~VulkanDriver() noexcept { DestroyResources(); }

DriverPtr VulkanDriver::Create(VulkanPlatform* platform, const VulkanContextPtr& context, const DriverConfig& config) {
    LOG_ASSERT(platform);
    DriverConfig validConfig    = config;
    validConfig.handleArenaSize = std::max(config.handleArenaSize, kMinHandleArenaSize);
    return new VulkanDriver(platform, context, validConfig);
}

Dispatcher VulkanDriver::GetDispatcher() const noexcept { return ConcreteDispatcher<VulkanDriver>::Make(); }

void VulkanDriver::DebugCommandBegin(CommandStream* cmds, bool synchronous, char const* methodName) noexcept {
    DriverBase::DebugCommandBegin(cmds, synchronous, methodName);
}

// 销毁顺序即资源依赖顺序：先让命令与各缓存放掉 Vk* 句柄，再清 ResourceManager，
// 最后才动分配器 —— 任何一步提前都会在 VMA 上留下存活分配
void VulkanDriver::DestroyResources() noexcept {
    // 幂等：terminate() 与析构都会走到这里；allocator 已释放即表示销毁已完成，
    // 再走一遍会让各组件在已销毁的 VkDevice 上重复 Terminate
    if (m_allocator == VK_NULL_HANDLE) {
        return;
    }

    // 排空队列并等待在途命令，后续各步才能安全释放被它们引用的对象
    // 此处不能走 finish()：它内部会从命令流分配命令，而 terminate() 可能来自非录制线程
    m_commands.Flush();
    m_commands.Wait();

    mCurrentSwapChain    = {};
    mDefaultRenderTarget = {};
    mPipelineState       = {};

    m_queryManager.Terminate();
    m_blitter.Terminate();
    m_readPixels.Terminate();

    // 1. 先让命令与各缓存放掉 VkPipeline / VkRenderPass / VkFramebuffer / VkSampler /
    //    VkDescriptorSet* / VkCommandPool
    m_commands.Terminate();

    m_pipelineCache.Terminate();
    m_framebufferCache.Terminate();
    m_samplerCache.Terminate();
    m_descriptorSetLayoutCache.Terminate();
    m_descriptorSetCache.Terminate();
    m_pipelineLayoutCache.Terminate();
    m_ycbcrConversionCache.Terminate();

    // 2. 清空引用计数资源：暂存段的回收回调会写回母缓冲，故必须早于 3 的池销毁
    if (m_resMgr) {
        m_resMgr->Terminate();
    }

    // 3. 释放各池持有的 VkBuffer / VkImage
    if (m_bufferCache) {
        m_bufferCache->Terminate();
        m_bufferCache.Reset();
    }
    if (m_stagePool) {
        m_stagePool->Terminate();
        m_stagePool.Reset();
    }

    if (m_semaphoreManager) {
        m_semaphoreManager->Terminate();
        m_semaphoreManager.Reset();
    }

    if (m_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }
}

void VulkanDriver::tick(int) {
    m_commands.UpdateFences();

    // 前端跳过大量帧时，flush() 可能久不调用，命令与资源会持续累积；
    // 这里保证 GC 与 flush 有独立于帧循环的节奏
    if (++m_ticksSinceLastGc == kMaxTicksBetweenGc) {
        flush(0);
        collectGarbage();
    }
}

void VulkanDriver::collectGarbage() {
    m_ticksSinceLastGc = 0;

    // 命令缓冲必须先提交完成，其它资源的回收才安全
    m_commands.Gc();
    m_descriptorSetCache.Gc();
    m_stagePool->Gc();
    m_bufferCache->Gc();
    m_framebufferCache.Gc();
    m_pipelineCache.Gc();

    m_resMgr->Gc();
    collectDescriptors();
}

void VulkanDriver::beginFrame(int64_t monotonic_clock_ns, int64_t refreshIntervalNs, uint32_t frameId) {
    // frameId 为 0 表示该帧不属于任何时序序列（独立视图）；本项目未接线
    // SetPresentFrameId（headless 路径无原生窗口），故只做 GC

    // 检查已完成的命令并释放其占用的资源：引用计数在这里下降，从而能判断
    // 某个 VulkanBuffer 是否还在途
    m_commands.Gc();
}

void VulkanDriver::SetFrameScheduledCallback(SwapChainHandle sch, CallbackHandler* handler, FrameScheduledCallback&& callback, uint64_t flags) {
    // 本项目未移植 PresentCallable，帧调度回调无落点；保留接口形态以便前端调用不失败
}

void VulkanDriver::SetFrameCompletedCallback(SwapChainHandle sch, CallbackHandler* handler, std::function<void()>&& callback) {}

void VulkanDriver::SetPresentationTime(int64_t monotonic_clock_ns) {}

void VulkanDriver::EndFrame(uint32_t frameId) {
    endCommandRecording();
    collectGarbage();
}

void VulkanDriver::flush(int) { endCommandRecording(); }

void VulkanDriver::finish(int) {
    endCommandRecording();

    // 只等缓冲自身的围栏不够：Present 也提交到同一条队列，故须等队列空闲
    vkQueueWaitIdle(mPlatform->GetVkGraphicsQueue());

    m_commands.Wait();
    m_readPixels.RunUntilComplete();

    m_pendingDescriptors.clear();
}

void VulkanDriver::resetState(int) {}

void VulkanDriver::deferDestroy(BufferDescriptor&& data) { m_pendingDescriptors.push_back({ std::move(data), m_commands.Get().Age() }); }

void VulkanDriver::deferDestroy(PixelBufferDescriptor&& data) { m_pendingDescriptors.push_back({ std::move(data), m_commands.Get().Age() }); }

void VulkanDriver::collectDescriptors() {
    // 命令缓冲最多同时存在 kMaxCommandBuffers 个，比该值更老的数据不可能还在被 GPU 读取
    uint32_t const currentAge = m_commands.Get().Age();
    while (!m_pendingDescriptors.empty() && currentAge - m_pendingDescriptors.front().submittedAge >= kMaxCommandBuffers) {
        m_pendingDescriptors.pop_front();
    }
}

void VulkanDriver::endCommandRecording() {
    m_commands.Flush();
    m_pipelineCache.ResetBoundPipeline();
    m_descriptorSetCache.ResetCachedState();
}

// ---------------------------------------------------------------- 资源创建与销毁

Handle<HwVertexBufferInfo> VulkanDriver::CreateVertexBufferInfoS() noexcept { return m_resMgr->AllocHandle<VulkanVertexBufferInfo>(); }

void VulkanDriver::CreateVertexBufferInfoR(VertexBufferInfoHandle vbih, uint8_t bufferCount, uint8_t attributeCount, AttributeArray attributes,
                                           NS_UTILS::ImmutableString&& tag) {
    m_resMgr->Make<VulkanVertexBufferInfo>(vbih, bufferCount, attributeCount, attributes);
    m_resMgr->AssociateTagToHandle(vbih.GetId(), std::move(tag));
}

void VulkanDriver::DestroyVertexBufferInfo(VertexBufferInfoHandle vbih) {
    if (!vbih) {
        return;
    }
    auto vbi = m_resMgr->Acquire<VulkanVertexBufferInfo>(vbih);
    m_resMgr->Destroy(vbi);
}

Handle<HwVertexBuffer> VulkanDriver::CreateVertexBufferS() noexcept { return m_resMgr->AllocHandle<VulkanVertexBuffer>(); }

void VulkanDriver::CreateVertexBufferR(VertexBufferHandle vbh, uint32_t vertexCount, VertexBufferInfoHandle vbInfoHandle,
                                       NS_UTILS::ImmutableString&& tag) {
    auto vbi = m_resMgr->Acquire<VulkanVertexBufferInfo>(vbInfoHandle);
    m_resMgr->Make<VulkanVertexBuffer>(vbh, m_context, m_bufferCache, vertexCount, vbi);
    m_resMgr->AssociateTagToHandle(vbh.GetId(), std::move(tag));
}

Handle<HwVertexBuffer> VulkanDriver::CreateVertexBufferAsyncS() noexcept { return m_resMgr->AllocHandle<VulkanVertexBuffer>(); }

void VulkanDriver::CreateVertexBufferAsyncR(VertexBufferHandle vbh, uint32_t vertexCount, VertexBufferInfoHandle vbih, CallbackHandler* handler,
                                            CallbackHandler::Callback callback, void* user, NS_UTILS::ImmutableString&& tag) {}

void VulkanDriver::DestroyVertexBuffer(VertexBufferHandle vbh) {
    if (!vbh) {
        return;
    }
    auto vb = m_resMgr->Acquire<VulkanVertexBuffer>(vbh);
    m_resMgr->Destroy(vb);
}

Handle<HwIndexBuffer> VulkanDriver::CreateIndexBufferS() noexcept { return m_resMgr->AllocHandle<VulkanIndexBuffer>(); }

void VulkanDriver::CreateIndexBufferR(IndexBufferHandle ibh, ElementType elementType, uint32_t indexCount, BufferUsage,
                                      NS_UTILS::ImmutableString&& tag) {
    auto const elementSize = static_cast<uint8_t>(Driver::GetElementTypeSize(elementType));
    m_resMgr->Make<VulkanIndexBuffer>(ibh, m_context, m_allocator, m_stagePool, m_bufferCache, elementSize, indexCount);
    m_resMgr->AssociateTagToHandle(ibh.GetId(), std::move(tag));
}

Handle<HwIndexBuffer> VulkanDriver::CreateIndexBufferAsyncS() noexcept { return m_resMgr->AllocHandle<VulkanIndexBuffer>(); }

void VulkanDriver::CreateIndexBufferAsyncR(IndexBufferHandle ibh, ElementType elementType, uint32_t indexCount, BufferUsage usage,
                                           CallbackHandler* handler, CallbackHandler::Callback callback, void* user,
                                           NS_UTILS::ImmutableString&& tag) {}

void VulkanDriver::DestroyIndexBuffer(IndexBufferHandle ibh) {
    if (!ibh) {
        return;
    }
    auto ib = m_resMgr->Acquire<VulkanIndexBuffer>(ibh);
    m_resMgr->Destroy(ib);
}

Handle<HwBufferObject> VulkanDriver::CreateBufferObjectS() noexcept { return m_resMgr->AllocHandle<VulkanBufferObject>(); }

void VulkanDriver::CreateBufferObjectR(BufferObjectHandle boh, uint32_t byteCount, BufferObjectBinding bindingType, BufferUsage usage,
                                       NS_UTILS::ImmutableString&& tag) {
    m_resMgr->Make<VulkanBufferObject>(boh, m_context, m_allocator, m_stagePool, m_bufferCache, byteCount, bindingType, usage);
    m_resMgr->AssociateTagToHandle(boh.GetId(), std::move(tag));
}

Handle<HwBufferObject> VulkanDriver::CreateBufferObjectAsyncS() noexcept { return m_resMgr->AllocHandle<VulkanBufferObject>(); }

void VulkanDriver::CreateBufferObjectAsyncR(BufferObjectHandle boh, uint32_t byteCount, BufferObjectBinding bindingType, BufferUsage usage,
                                            CallbackHandler* handler, CallbackHandler::Callback callback, void* user,
                                            NS_UTILS::ImmutableString&& tag) {}

void VulkanDriver::DestroyBufferObject(BufferObjectHandle boh) {
    if (!boh) {
        return;
    }
    auto bo = m_resMgr->Acquire<VulkanBufferObject>(boh);
    m_resMgr->Destroy(bo);
}

void VulkanDriver::SetVertexBufferObject(VertexBufferHandle vbh, uint32_t index, BufferObjectHandle boh) {
    auto vb = m_resMgr->Acquire<VulkanVertexBuffer>(vbh);
    auto bo = m_resMgr->Acquire<VulkanBufferObject>(boh);

    LOG_ASSERT(bo->bindingType == BufferObjectBinding::Vertex);
    vb->SetBuffer(bo, index);
}

AsyncCallId VulkanDriver::SetVertexBufferObjectAsyncS() noexcept {
    // 上游同样未实现：异步变体无调用点
    return 0;
}

void VulkanDriver::SetVertexBufferObjectAsyncR(AsyncCallId jobId, VertexBufferHandle vbh, uint32_t index, BufferObjectHandle bufferObject,
                                               CallbackHandler* handler, CallbackHandler::Callback callback, void* user) {}

void VulkanDriver::UpdateIndexBuffer(IndexBufferHandle ibh, BufferDescriptor&& data, uint32_t byteOffset) {
    auto ib = m_resMgr->Acquire<VulkanIndexBuffer>(ibh);
    ib->LoadFromCpu(m_commands.Get(), data.buffer, byteOffset, static_cast<uint32_t>(data.size));
    deferDestroy(std::move(data));
}

AsyncCallId VulkanDriver::UpdateIndexBufferAsyncS() noexcept { return 0; }

void VulkanDriver::UpdateIndexBufferAsyncR(AsyncCallId jobId, IndexBufferHandle ibh, BufferDescriptor&& data, uint32_t byteOffset,
                                           CallbackHandler* handler, CallbackHandler::Callback callback, void* user) {}

void VulkanDriver::UpdateBufferObject(BufferObjectHandle boh, BufferDescriptor&& data, uint32_t byteOffset) {
    auto bo = m_resMgr->Acquire<VulkanBufferObject>(boh);
    bo->LoadFromCpu(m_commands.Get(), data.buffer, byteOffset, static_cast<uint32_t>(data.size));
    deferDestroy(std::move(data));
}

AsyncCallId VulkanDriver::UpdateBufferObjectAsyncS() noexcept { return 0; }

void VulkanDriver::UpdateBufferObjectAsyncR(AsyncCallId jobId, BufferObjectHandle boh, BufferDescriptor&& data, uint32_t byteOffset,
                                            CallbackHandler* handler, CallbackHandler::Callback callback, void* user) {}

void VulkanDriver::UpdateBufferObjectUnsynchronized(BufferObjectHandle boh, BufferDescriptor&& data, uint32_t byteOffset) {
    // 上游同样走同步路径（未实现真正的无同步上传），保留其行为
    UpdateBufferObject(boh, std::move(data), byteOffset);
}

void VulkanDriver::ResetBufferObject(BufferObjectHandle boh) {
    // 上游为空实现：只有 updateBufferObjectUnsynchronized 真正无同步时，把旧缓冲孤立才有意义
}

void VulkanDriver::Update3DImage(TextureHandle th, uint32_t level, uint32_t xoffset, uint32_t yoffset, uint32_t zoffset, uint32_t width,
                                 uint32_t height, uint32_t depth, PixelBufferDescriptor&& data) {
    auto texture = m_resMgr->Acquire<VulkanTexture>(th);
    texture->UpdateImage(data, width, height, depth, xoffset, yoffset, zoffset, level);
    deferDestroy(std::move(data));
}

AsyncCallId VulkanDriver::Update3DImageAsyncS() noexcept { return 0; }

void VulkanDriver::Update3DImageAsyncR(AsyncCallId jobId, TextureHandle th, uint32_t level, uint32_t xoffset, uint32_t yoffset, uint32_t zoffset,
                                       uint32_t width, uint32_t height, uint32_t depth, PixelBufferDescriptor&& data, CallbackHandler* handler,
                                       CallbackHandler::Callback callback, void* user) {}

Handle<HwTexture> VulkanDriver::CreateTextureS() noexcept { return m_resMgr->AllocHandle<VulkanTexture>(); }

void VulkanDriver::CreateTextureR(TextureHandle th, SamplerType target, uint8_t levels, TextureFormat format, uint8_t samples, uint32_t width,
                                  uint32_t height, uint32_t depth, TextureUsage usage, NS_UTILS::ImmutableString&& tag) {
    auto texture = m_resMgr->Make<VulkanTexture>(th, mPlatform->GetVkDevice(), mPlatform->GetVkPhysicalDevice(), m_context, m_allocator, m_resMgr,
                                                 &m_commands, target, levels, format, samples, width, height, depth, usage, m_stagePool);

    // 新建纹理须立即转换到默认布局，否则首次作为附件或采样源时布局不匹配
    VulkanCommandBuffer& commands = m_commands.Get();
    texture->TransitionLayout(&commands, texture->GetPrimaryViewRange(), texture->GetDefaultLayout());

    m_resMgr->AssociateTagToHandle(th.GetId(), std::move(tag));
}

Handle<HwTexture> VulkanDriver::CreateTextureAsyncS() noexcept { return m_resMgr->AllocHandle<VulkanTexture>(); }

void VulkanDriver::CreateTextureAsyncR(TextureHandle th, SamplerType target, uint8_t levels, TextureFormat format, uint8_t samples, uint32_t width,
                                       uint32_t height, uint32_t depth, TextureUsage usage, CallbackHandler* handler,
                                       CallbackHandler::Callback callback, void* user, NS_UTILS::ImmutableString&& tag) {}

Handle<HwTexture> VulkanDriver::CreateTextureViewS() noexcept { return m_resMgr->AllocHandle<VulkanTexture>(); }

void VulkanDriver::CreateTextureViewR(TextureHandle th, TextureHandle texture, uint8_t baseLevel, uint8_t levelCount,
                                      NS_UTILS::ImmutableString&& tag) {
    auto src = m_resMgr->Acquire<VulkanTexture>(texture);
    m_resMgr->Make<VulkanTexture>(th, mPlatform->GetVkDevice(), mPlatform->GetVkPhysicalDevice(), m_context, m_allocator, &m_commands, src, baseLevel,
                                  levelCount);
    m_resMgr->AssociateTagToHandle(th.GetId(), std::move(tag));
}

Handle<HwTexture> VulkanDriver::CreateTextureViewSwizzleS() noexcept { return m_resMgr->AllocHandle<VulkanTexture>(); }

void VulkanDriver::CreateTextureViewSwizzleR(TextureHandle th, TextureHandle texture, TextureSwizzle r, TextureSwizzle g, TextureSwizzle b,
                                             TextureSwizzle a, NS_UTILS::ImmutableString&& tag) {
    TextureSwizzle const     swizzleArray[] = { r, g, b, a };
    VkComponentMapping const swizzle        = VK_UTILS::GetSwizzleMap(swizzleArray);
    auto                     src            = m_resMgr->Acquire<VulkanTexture>(texture);
    m_resMgr->Make<VulkanTexture>(th, mPlatform->GetVkDevice(), mPlatform->GetVkPhysicalDevice(), m_context, m_allocator, &m_commands, src, swizzle);
    m_resMgr->AssociateTagToHandle(th.GetId(), std::move(tag));
}

Handle<HwTexture> VulkanDriver::CreateTextureViewSwizzleAsyncS() noexcept { return m_resMgr->AllocHandle<VulkanTexture>(); }

void VulkanDriver::CreateTextureViewSwizzleAsyncR(TextureHandle th, TextureHandle texture, TextureSwizzle r, TextureSwizzle g, TextureSwizzle b,
                                                  TextureSwizzle a, CallbackHandler* handler, CallbackHandler::Callback callback, void* user,
                                                  NS_UTILS::ImmutableString&& tag) {}

void VulkanDriver::DestroyTexture(TextureHandle th) {
    if (!th) {
        return;
    }
    auto texture = m_resMgr->Acquire<VulkanTexture>(th);
    m_resMgr->Destroy(texture);
}

void VulkanDriver::GenerateMipmaps(TextureHandle th) {
    auto t = m_resMgr->Acquire<VulkanTexture>(th);
    LOG_ASSERT(t);

    int32_t layerCount = static_cast<int32_t>(t->depth);
    if (t->target == SamplerType::SAMPLER_CUBEMAP || t->target == SamplerType::SAMPLER_CUBEMAP_ARRAY) {
        layerCount *= 6;
    }

    LOG_ASSERT(layerCount < (1 << (sizeof(VulkanAttachment::layerCount) * 8)));

    // 逐级 blit：每级都把上一级缩半拷进下一级，随后整体转回默认布局
    uint8_t level = 0;
    int32_t srcw  = static_cast<int32_t>(t->width);
    int32_t srch  = static_cast<int32_t>(t->height);
    do {
        int32_t const    dstw          = std::max(srcw >> 1, 1);
        int32_t const    dsth          = std::max(srch >> 1, 1);
        VkOffset3D const srcOffsets[2] = { { 0, 0, 0 }, { srcw, srch, 1 } };
        VkOffset3D const dstOffsets[2] = { { 0, 0, 0 }, { dstw, dsth, 1 } };

        for (uint8_t layer = 0; layer < layerCount; layer++) {
            VulkanAttachment dst{ .level = static_cast<uint8_t>(level + 1), .layer = layer };
            dst.texture = t;
            VulkanAttachment src{ .level = level, .layer = layer };
            src.texture = t;
            m_blitter.Blit(VK_FILTER_LINEAR, dst, dstOffsets, src, srcOffsets);
        }

        srcw = dstw;
        srch = dsth;
    } while ((srcw > 1 || srch > 1) && ++level < t->levels - 1);

    VulkanCommandBuffer* commandBuffer = &m_commands.Get();
    t->TransitionLayout(commandBuffer, t->GetPrimaryViewRange(), t->GetDefaultLayout());
}

void VulkanDriver::CreateTextureExternalImage2R(TextureHandle th, SamplerType target, TextureFormat format, uint32_t width, uint32_t height,
                                                TextureUsage usage, Platform::ExternalImageHandleRef image, NS_UTILS::ImmutableString&& tag) {
    LOG_WARN("CreateTextureExternalImage2 未实现：外部图像路径已按设计砍掉");
}

Handle<HwTexture> VulkanDriver::CreateTextureExternalImage2S() noexcept { return m_resMgr->AllocHandle<VulkanTexture>(); }

void VulkanDriver::CreateTextureExternalImageR(TextureHandle th, SamplerType target, TextureFormat format, uint32_t width, uint32_t height,
                                               TextureUsage usage, void* image, NS_UTILS::ImmutableString&& tag) {
    LOG_WARN("CreateTextureExternalImage 未实现：外部图像路径已按设计砍掉");
}

Handle<HwTexture> VulkanDriver::CreateTextureExternalImageS() noexcept { return m_resMgr->AllocHandle<VulkanTexture>(); }

void VulkanDriver::CreateTextureExternalImagePlaneR(TextureHandle th, TextureFormat format, uint32_t width, uint32_t height, TextureUsage usage,
                                                    void* image, uint32_t plane, NS_UTILS::ImmutableString&& tag) {
    LOG_WARN("CreateTextureExternalImagePlane 未实现：外部图像路径已按设计砍掉");
}

Handle<HwTexture> VulkanDriver::CreateTextureExternalImagePlaneS() noexcept { return m_resMgr->AllocHandle<VulkanTexture>(); }

void VulkanDriver::ImportTextureR(TextureHandle th, intptr_t id, SamplerType target, uint8_t levels, TextureFormat format, uint8_t samples,
                                  uint32_t width, uint32_t height, uint32_t depth, TextureUsage usage, NS_UTILS::ImmutableString&& tag) {
    LOG_WARN("ImportTexture 未实现：外部图像路径已按设计砍掉");
}

Handle<HwTexture> VulkanDriver::ImportTextureS() noexcept { return m_resMgr->AllocHandle<VulkanTexture>(); }

void VulkanDriver::ImportTextureAsyncR(TextureHandle th, intptr_t id, SamplerType target, uint8_t levels, TextureFormat format, uint8_t samples,
                                       uint32_t width, uint32_t height, uint32_t depth, TextureUsage usage, CallbackHandler* handler,
                                       CallbackHandler::Callback callback, void* user, NS_UTILS::ImmutableString&& tag) {
    LOG_WARN("ImportTextureAsync 未实现：外部图像路径已按设计砍掉");
}

Handle<HwTexture> VulkanDriver::ImportTextureAsyncS() noexcept { return m_resMgr->AllocHandle<VulkanTexture>(); }

void VulkanDriver::SetupExternalImage2(Platform::ExternalImageHandleRef image) { LOG_WARN("SetupExternalImage2 未实现：外部图像路径已按设计砍掉"); }

void VulkanDriver::SetupExternalImage(void* image) { LOG_WARN("SetupExternalImage 未实现：外部图像路径已按设计砍掉"); }

Handle<HwRenderPrimitive> VulkanDriver::CreateRenderPrimitiveS() noexcept { return m_resMgr->AllocHandle<VulkanRenderPrimitive>(); }

void VulkanDriver::CreateRenderPrimitiveR(RenderPrimitiveHandle rph, VertexBufferHandle vbh, IndexBufferHandle ibh, PrimitiveType pt,
                                          NS_UTILS::ImmutableString&& tag) {
    auto vb = m_resMgr->Acquire<VulkanVertexBuffer>(vbh);
    // 无索引缓冲的图元合法：走 vkCmdDraw 而非 vkCmdDrawIndexed
    VulkanIndexBufferPtr ib;
    if (ibh) {
        ib = m_resMgr->Acquire<VulkanIndexBuffer>(ibh);
    }
    m_resMgr->Make<VulkanRenderPrimitive>(rph, pt, vb, ib);
    m_resMgr->AssociateTagToHandle(rph.GetId(), std::move(tag));
}

void VulkanDriver::DestroyRenderPrimitive(RenderPrimitiveHandle rph) {
    if (!rph) {
        return;
    }
    auto prim = m_resMgr->Acquire<VulkanRenderPrimitive>(rph);
    m_resMgr->Destroy(prim);
}

Handle<HwProgram> VulkanDriver::CreateProgramS() noexcept { return m_resMgr->AllocHandle<VulkanProgram>(); }

void VulkanDriver::CreateProgramR(ProgramHandle ph, Program&& program, NS_UTILS::ImmutableString&& tag) {
    auto vprogram = m_resMgr->Make<VulkanProgram>(ph, mPlatform->GetVkDevice(), program);
    m_resMgr->AssociateTagToHandle(ph.GetId(), std::move(tag));

    if (!m_context->IsPipelineCachePrewarmingEnabled()) {
        return;
    }

    // 并行预编译打开时，先就地把布局建出来并预热基础管线
    std::array<VulkanDescriptorSetLayoutPtr, MAX_DESCRIPTOR_SET_COUNT> layouts{};
    VulkanDescriptorSetLayout::DescriptorSetLayoutArray                vkLayouts{};
    bool                                                               hasExternalSamplers = false;
    for (auto const& layoutBinding : program.GetDescriptorSetLayouts()) {
        DescriptorSetLayout layoutDescription = layoutBinding.layout;
        auto                layoutHandle      = m_resMgr->AllocHandle<VulkanDescriptorSetLayout>();
        auto                layout            = m_descriptorSetLayoutCache.CreateLayout(layoutHandle, std::move(layoutDescription));
        layouts[layoutBinding.set]            = layout;
        vkLayouts[layoutBinding.set]          = layout->GetVkLayout();
        if (layout->HasExternalSamplers()) {
            hasExternalSamplers = true;
        }
    }

    StereoscopicType stereoscopicType = mStereoscopicType;
    if (stereoscopicType == StereoscopicType::Multiview && !program.IsMultiview()) {
        stereoscopicType = StereoscopicType::None;
    }

    m_pipelineCache.AsyncPrewarmCache(vprogram, m_pipelineLayoutCache.GetLayout(vkLayouts, vprogram), stereoscopicType, mStereoscopicEyeCount,
                                      program.GetPriorityQueue());

    if (!hasExternalSamplers) {
        return;
    }

    for (auto const& format : m_context->GetPipelineCachePrewarmExternalFormats()) {
        VkSamplerYcbcrConversion const vkConversion    = m_ycbcrConversionCache.GetConversion(GetYcbcrConversionParams(format));
        VkSampler const                externalSampler = m_samplerCache.GetSampler({ .sampler = {}, .conversion = vkConversion });

        for (size_t i = 0; i < MAX_DESCRIPTOR_SET_COUNT; ++i) {
            if (!layouts[i]) {
                continue;
            }
            // 预热只需要「大致像」的采样器组合：遍历可能出现的采样器类型即可命中驱动缓存
            std::vector<std::pair<uint64_t, VkSampler>> externalSamplers(layouts[i]->bitmask.externalSampler.Count(), { 0, externalSampler });
            vkLayouts[i] = m_descriptorSetLayoutCache.GetVkLayout(layouts[i]->bitmask, layouts[i]->bitmask.externalSampler, externalSamplers);
        }

        m_pipelineCache.AsyncPrewarmCache(vprogram, m_pipelineLayoutCache.GetLayout(vkLayouts, vprogram), stereoscopicType, mStereoscopicEyeCount,
                                          program.GetPriorityQueue());
    }
}

void VulkanDriver::DestroyProgram(ProgramHandle ph) {
    if (!ph) {
        return;
    }
    auto vprogram = m_resMgr->Acquire<VulkanProgram>(ph);
    vprogram->CancelParallelCompilation();
    m_resMgr->Destroy(vprogram);
}

void VulkanDriver::CompilePrograms(CompilerPriorityQueue priority, CallbackHandler* handler, CallbackHandler::Callback callback, void* user) {
    if (callback) {
        if (m_context->IsPipelineCachePrewarmingEnabled()) {
            m_pipelineCache.AddCachePrewarmCallback(handler, callback, user);
        } else {
            ScheduleCallback(handler, user, callback);
        }
    }
}

Handle<HwRenderTarget> VulkanDriver::CreateDefaultRenderTargetS() noexcept { return m_resMgr->AllocHandle<VulkanRenderTarget>(); }

void VulkanDriver::CreateDefaultRenderTargetR(RenderTargetHandle rth, NS_UTILS::ImmutableString&& tag) {
    // 构造期已建好默认渲染目标，此处只是把内容交给新句柄
    LOG_ASSERT(mDefaultRenderTarget);
    mDefaultRenderTarget = m_resMgr->Make<VulkanRenderTarget>(rth, std::move(*mDefaultRenderTarget.Get()));
    m_resMgr->AssociateTagToHandle(rth.GetId(), std::move(tag));
}

Handle<HwRenderTarget> VulkanDriver::CreateRenderTargetS() noexcept { return m_resMgr->AllocHandle<VulkanRenderTarget>(); }

void VulkanDriver::CreateRenderTargetR(RenderTargetHandle rth, TargetBufferFlags targetBufferFlags, uint32_t width, uint32_t height, uint8_t samples,
                                       uint8_t layerCount, MRT color, TargetBufferInfo depth, TargetBufferInfo stencil,
                                       NS_UTILS::ImmutableString&& tag) {
    size_t           attachmentCount                                      = 0;
    VulkanAttachment colorTargets[MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT] = {};
    for (int i = 0; i < MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT; i++) {
        if (color[i].handle) {
            colorTargets[i] = {
                .texture    = m_resMgr->Acquire<VulkanTexture>(color[i].handle),
                .level      = color[i].level,
                .layerCount = layerCount,
                .layer      = static_cast<uint8_t>(color[i].layer),
            };
            attachmentCount++;
        }
    }

    // VK 的渲染目标只能有一个深度/模板附件，深度与模板合并时由 depth 提供
    VulkanAttachment depthStencil;
    if (depth.handle || stencil.handle) {
        LOG_ASSERT(!depth.handle || !stencil.handle || (depth.handle == stencil.handle));
        TargetBufferInfo const depthStencilBuffer = depth.handle ? depth : stencil;
        depthStencil                              = {
                                         .texture    = m_resMgr->Acquire<VulkanTexture>(depthStencilBuffer.handle),
                                         .level      = depthStencilBuffer.level,
                                         .layerCount = layerCount,
                                         .layer      = static_cast<uint8_t>(depthStencilBuffer.layer),
        };
        attachmentCount++;
    }

    LOG_ASSERT(attachmentCount > 0);

    m_resMgr->Make<VulkanRenderTarget>(rth, mPlatform->GetVkDevice(), mPlatform->GetVkPhysicalDevice(), m_context, m_resMgr, m_allocator, &m_commands,
                                       width, height, samples, colorTargets, depthStencil, m_stagePool, layerCount);

    m_resMgr->AssociateTagToHandle(rth.GetId(), std::move(tag));
}

void VulkanDriver::DestroyRenderTarget(RenderTargetHandle rth) {
    if (!rth) {
        return;
    }
    auto rt = m_resMgr->Acquire<VulkanRenderTarget>(rth);
    // 默认渲染目标由 terminate() 统一回收，不能在这里归零
    if (rt.Get() != mDefaultRenderTarget.Get()) {
        m_resMgr->Destroy(rt);
    }
}

Handle<HwSwapChain> VulkanDriver::CreateSwapChainS() noexcept { return m_resMgr->AllocHandle<VulkanSwapChain>(); }

void VulkanDriver::CreateSwapChainR(SwapChainHandle sch, void* nativeWindow, uint64_t flags, NS_UTILS::ImmutableString&& tag) {
    // 旧交换链必须先释放，否则 vkCreateSwapchainKHR 会以 VK_ERROR_NATIVE_WINDOW_IN_USE_KHR 失败
    m_resMgr->Gc();

    if ((flags & kSwapChainConfigSRGBColorspace) != 0 && !IsSRGBSwapChainSupported()) {
        LOG_WARN("sRGB swapchain requested, but Platform does not support it");
        flags = flags & ~kSwapChainConfigSRGBColorspace;
    }
    if ((flags & kSwapChainConfigProtectedContent) != 0 && !IsProtectedContentSupported()) {
        LOG_WARN("protected swapchain requested, but Platform does not support it");
    }

    m_resMgr->Make<VulkanSwapChain>(sch, mPlatform, m_context, m_resMgr, m_allocator, &m_commands, m_stagePool, nativeWindow, flags);
    m_resMgr->AssociateTagToHandle(sch.GetId(), std::move(tag));

    std::lock_guard const lock(mTiming.lock);
    mTiming.nativeSwapchains.emplace(sch.GetId(), nullptr);
}

Handle<HwSwapChain> VulkanDriver::CreateSwapChainHeadlessS() noexcept { return m_resMgr->AllocHandle<VulkanSwapChain>(); }

void VulkanDriver::CreateSwapChainHeadlessR(SwapChainHandle sch, uint32_t width, uint32_t height, uint64_t flags, NS_UTILS::ImmutableString&& tag) {
    if ((flags & kSwapChainConfigSRGBColorspace) != 0 && !IsSRGBSwapChainSupported()) {
        LOG_WARN("sRGB swapchain requested, but Platform does not support it");
        flags = flags & ~kSwapChainConfigSRGBColorspace;
    }
    LOG_ASSERT(width > 0 && height > 0 && "Vulkan requires non-zero swap chain dimensions.");

    m_resMgr->Make<VulkanSwapChain>(sch, mPlatform, m_context, m_resMgr, m_allocator, &m_commands, m_stagePool, nullptr, flags,
                                    VkExtent2D{ width, height });
    m_resMgr->AssociateTagToHandle(sch.GetId(), std::move(tag));
}

void VulkanDriver::DestroySwapChain(SwapChainHandle sch) {
    if (!sch) {
        return;
    }
    auto swapChain = m_resMgr->Acquire<VulkanSwapChain>(sch);
    if (mCurrentSwapChain.Get() == swapChain.Get()) {
        mCurrentSwapChain = {};
    }
    m_resMgr->Destroy(swapChain);

    std::lock_guard const lock(mTiming.lock);
    mTiming.nativeSwapchains.erase(sch.GetId());
}

Handle<HwSync> VulkanDriver::CreateSyncS() noexcept {
    auto handle = m_resMgr->AllocHandle<VulkanSync>();
    m_resMgr->Make<VulkanSync>(handle);
    return handle;
}

void VulkanDriver::CreateSyncR(SyncHandle sh, NS_UTILS::ImmutableString&& tag) {
    auto                            sync = m_resMgr->Acquire<VulkanSync>(sh);
    std::shared_ptr<VulkanCmdFence> fenceStatus;
    if (mCurrentRenderPass.commandBuffer) {
        fenceStatus = mCurrentRenderPass.commandBuffer->GetFenceStatus();
        // 正在录制：先提交，围栏才只覆盖已发出的命令
        m_commands.Flush();
    } else {
        fenceStatus = m_commands.GetMostRecentFenceStatus();
    }

    {
        std::lock_guard const guard(sync->GetLock());
        sync->sync = mPlatform->CreateSync(fenceStatus);
    }

    for (auto& cbData : sync->GetConversionCallbacks()) {
        cbData->sync = sync->sync;
        ScheduleCallback(cbData->handler, cbData.release(), syncCallbackWrapper);
    }
    sync->GetConversionCallbacks().clear();
    m_resMgr->AssociateTagToHandle(sh.GetId(), std::move(tag));
}

void VulkanDriver::DestroySync(SyncHandle sh) {
    if (!sh) {
        return;
    }
    auto sync = m_resMgr->Acquire<VulkanSync>(sh);
    mPlatform->DestroySync(sync->sync);
    m_resMgr->Destroy(sync);
}

void VulkanDriver::GetPlatformSync(SyncHandle sh, CallbackHandler* handler, Platform::SyncCallback cb, void* userData) {
    auto sync        = m_resMgr->Acquire<VulkanSync>(sh);
    auto cbData      = std::make_unique<VulkanSync::CallbackData>();
    cbData->handler  = handler;
    cbData->cb       = cb;
    cbData->userData = userData;

    // 平台同步对象尚未就绪时先入队，等 CreateSyncR 就绪后统一派发
    {
        std::lock_guard const guard(sync->GetLock());
        if (sync->sync == nullptr) {
            sync->GetConversionCallbacks().push_back(std::move(cbData));
            return;
        }
    }

    cbData->sync = sync->sync;
    ScheduleCallback(cbData->handler, cbData.release(), syncCallbackWrapper);
}

FenceHandle VulkanDriver::createFenceS() noexcept {
    auto handle = m_resMgr->AllocHandle<VulkanFence>();
    m_resMgr->Make<VulkanFence>(handle);
    return handle;
}

void VulkanDriver::createFenceR(FenceHandle fh, NS_UTILS::ImmutableString&& tag) {
    VulkanCommandBuffer* cmdbuf = mCurrentRenderPass.commandBuffer ? mCurrentRenderPass.commandBuffer : &m_commands.Get();
    // 句柄已在 createFenceS 里构造好，这里只补上与当前录制命令缓冲对应的围栏
    auto fence = m_resMgr->Acquire<VulkanFence>(fh);
    SignalFence([&] { fence->SetFence(cmdbuf->GetFenceStatus()); });
    m_resMgr->AssociateTagToHandle(fh.GetId(), std::move(tag));
}

void VulkanDriver::destroyFence(FenceHandle fh) {
    if (!fh) {
        return;
    }
    // 与 fenceWait() 并发调用销毁是非法的；此时没有等待者，故不必唤醒
    auto fence = m_resMgr->Acquire<VulkanFence>(fh);
    m_resMgr->Destroy(fence);
}

void VulkanDriver::FenceCancel(FenceHandle fh) {
    // 同步调用，句柄必须有效且保持有效
    LOG_ASSERT(fh);
    auto fence = m_resMgr->Acquire<VulkanFence>(fh);
    SignalFence([&] { fence->Cancel(); });
}

FenceStatus VulkanDriver::GetFenceStatus(FenceHandle fh) { return FenceWait(fh, 0); }

FenceStatus VulkanDriver::FenceWait(FenceHandle fh, uint64_t timeout) {
    LOG_ASSERT(fh);

    auto fence = m_resMgr->Acquire<VulkanFence>(fh);

    using namespace std::chrono;
    auto const               now   = steady_clock::now();
    steady_clock::time_point until = steady_clock::time_point::max();

    // 超时值须同时考虑 nanoseconds 与调用方类型两个方向的溢出
    using TimeoutType                = decltype(timeout);
    constexpr TimeoutType maxTimeout = std::numeric_limits<TimeoutType>::max();
    constexpr nanoseconds maxNano    = nanoseconds::max();
    if (timeout < maxNano.count() && timeout < maxTimeout && now <= steady_clock::time_point::max() - nanoseconds(timeout)) {
        until = now + nanoseconds(timeout);
    }

    std::shared_ptr<VulkanCmdFence> cmdfence;
    bool                            canceled = false;
    FenceStatus                     status   = WaitForFence(
        [&] {
            auto fstatus = fence->GetStatus();
            if (bool(fstatus.first) || fstatus.second) {
                cmdfence = fstatus.first;
                canceled = fstatus.second;
                return true;
            }
            return false;
        },
        until);

    if (status == FenceStatus::Error) {
        return FenceStatus::Error;
    }

    if (!cmdfence || canceled) {
        return canceled ? FenceStatus::Error : FenceStatus::TimeoutExpired;
    }

    // 此处持有 VulkanCmdFence 引用，等待期间它不会被回收
    return cmdfence->Wait(mPlatform->GetVkDevice(), timeout, until);
}

Handle<HwTimerQuery> VulkanDriver::CreateTimerQueryS() noexcept {
    // 句柄必须在此构造：getTimerQueryValue 的同步调用可能先于 CreateTimerQueryR 发生
    auto query = m_queryManager.GetNextQuery(m_resMgr);
    return Handle<HwTimerQuery>(query->GetId());
}

void VulkanDriver::CreateTimerQueryR(TimerQueryHandle tqh, NS_UTILS::ImmutableString&& tag) {
    // 计时器在 CreateTimerQueryS 里已构造完成
    m_resMgr->AssociateTagToHandle(tqh.GetId(), std::move(tag));
}

void VulkanDriver::DestroyTimerQuery(TimerQueryHandle tqh) {
    if (!tqh) {
        return;
    }
    auto vtq = m_resMgr->Acquire<VulkanTimerQuery>(tqh);
    m_queryManager.ClearQuery(vtq);
    m_resMgr->Destroy(vtq);
}

void VulkanDriver::BeginTimerQuery(TimerQueryHandle tqh) {
    auto vtq = m_resMgr->Acquire<VulkanTimerQuery>(tqh);
    m_queryManager.BeginQuery(&m_commands.Get(), vtq);
}

void VulkanDriver::EndTimerQuery(TimerQueryHandle tqh) {
    auto vtq = m_resMgr->Acquire<VulkanTimerQuery>(tqh);
    m_queryManager.EndQuery(&m_commands.Get(), vtq);
}

TimerQueryResult VulkanDriver::GetTimerQueryValue(TimerQueryHandle tqh, uint64_t* elapsedTime) {
    auto vtq = m_resMgr->Acquire<VulkanTimerQuery>(tqh);
    if (!vtq->IsCompleted()) {
        return TimerQueryResult::NotReady;
    }

    auto const results = m_queryManager.GetResult(vtq);
    if (results.beginAvailable == 0 || results.endAvailable == 0) {
        return TimerQueryResult::NotReady;
    }

    uint64_t const begin = results.beginTime;
    uint64_t const end   = results.endTime;
    if (begin >= end) {
        // 计时器可能落在不同的命令缓冲上，时间戳不再单调
        LOG_WARN("Timestamps are not monotonically increasing.");
        *elapsedTime = 0;
        return TimerQueryResult::Error;
    }

    // 纳秒换算在驱动侧做；MoltenVK 目前写入的是系统时间，故 delta 恒为 0
    float const period = m_context->GetPhysicalDeviceLimits().timestampPeriod;
    *elapsedTime       = static_cast<uint64_t>(static_cast<float>(end - begin) * period);
    return TimerQueryResult::Available;
}

Handle<HwDescriptorSetLayout> VulkanDriver::CreateDescriptorSetLayoutS() noexcept { return m_resMgr->AllocHandle<VulkanDescriptorSetLayout>(); }

void VulkanDriver::CreateDescriptorSetLayoutR(DescriptorSetLayoutHandle dslh, DescriptorSetLayout&& info, NS_UTILS::ImmutableString&& tag) {
    m_descriptorSetLayoutCache.CreateLayout(dslh, std::move(info));
    m_resMgr->AssociateTagToHandle(dslh.GetId(), std::move(tag));
}

void VulkanDriver::DestroyDescriptorSetLayout(DescriptorSetLayoutHandle dslh) {
    if (!dslh) {
        return;
    }
    auto layout = m_resMgr->Acquire<VulkanDescriptorSetLayout>(dslh);
    m_resMgr->Destroy(layout);
}

Handle<HwDescriptorSet> VulkanDriver::CreateDescriptorSetS() noexcept { return m_resMgr->AllocHandle<VulkanDescriptorSet>(); }

void VulkanDriver::CreateDescriptorSetR(DescriptorSetHandle dsh, DescriptorSetLayoutHandle dslh, NS_UTILS::ImmutableString&& tag) {
    // 句柄实参类型必须是具体类型：池块尺寸取自该类型
    auto layout = m_resMgr->Acquire<VulkanDescriptorSetLayout>(dslh);
    m_descriptorSetCache.CreateSet(dsh, layout);

    if (layout->HasExternalSamplers()) {
        mAppState.hasExternalSamplerLayouts = true;
    }

    m_resMgr->AssociateTagToHandle(dsh.GetId(), std::move(tag));
}

void VulkanDriver::DestroyDescriptorSet(DescriptorSetHandle dsh) {
    if (!dsh) {
        return;
    }
    auto set = m_resMgr->Acquire<VulkanDescriptorSet>(dsh);
    m_resMgr->Destroy(set);
}

void VulkanDriver::UpdateDescriptorSetBuffer(DescriptorSetHandle dsh, descriptor_binding_t binding, BufferObjectHandle boh, uint32_t offset,
                                             uint32_t size) {
    auto set    = m_resMgr->Acquire<VulkanDescriptorSet>(dsh);
    auto buffer = m_resMgr->Acquire<VulkanBufferObject>(boh);
    m_descriptorSetCache.UpdateBuffer(set, binding, buffer, offset, size);
}

void VulkanDriver::UpdateDescriptorSetTexture(DescriptorSetHandle dsh, descriptor_binding_t binding, TextureHandle th, SamplerParams params) {
    auto set     = m_resMgr->Acquire<VulkanDescriptorSet>(dsh);
    auto texture = m_resMgr->Acquire<VulkanTexture>(th);

    // 外部图像/流的不可变采样器路径未移植：一律按普通采样器写入
    VulkanSamplerCache::Params const cacheParams = { .sampler = params };
    VkSampler const                  vksampler   = m_samplerCache.GetSampler(cacheParams);
    m_descriptorSetCache.UpdateSampler(set, binding, texture, vksampler);
}

MemoryMappedBufferHandle VulkanDriver::MapBufferS() noexcept { return m_resMgr->AllocHandle<VulkanMemoryMappedBuffer>(); }

void VulkanDriver::MapBufferR(MemoryMappedBufferHandle mmbh, BufferObjectHandle boh, size_t offset, size_t size, MapBufferAccessFlags access,
                              NS_UTILS::ImmutableString&& tag) {
    auto mmb = m_resMgr->Make<VulkanMemoryMappedBuffer>(mmbh, boh, offset, size, access);
    m_resMgr->AssociateTagToHandle(mmbh.GetId(), std::move(tag));
}

void VulkanDriver::UnmapBuffer(MemoryMappedBufferHandle mmbh) {
    if (!mmbh) {
        return;
    }
    auto mmb = m_resMgr->Acquire<VulkanMemoryMappedBuffer>(mmbh);
    m_resMgr->Destroy(mmb);
}

void VulkanDriver::CopyToMemoryMappedBuffer(MemoryMappedBufferHandle mmbh, size_t offset, BufferDescriptor&& data) {
    auto mmb = m_resMgr->Acquire<VulkanMemoryMappedBuffer>(mmbh);

    LOG_ASSERT(HasAnyFlag(mmb->access, MapBufferAccessFlags::WRITE_BIT));
    LOG_ASSERT(offset + data.size <= mmb->size);

    // UMA 上这是直接 memcpy 进映射内存；否则会走暂存缓冲
    UpdateBufferObject(mmb->boh, std::move(data), static_cast<uint32_t>(mmb->offset + offset));
}

// ---------------------------------------------------------------- 绘制路径

void VulkanDriver::BeginRenderPass(RenderTargetHandle rth, const RenderPassParams& params) {
    auto rt = m_resMgr->Acquire<VulkanRenderTarget>(rth);

    // 前端期望交换链内容在首个渲染通道上不被保留；后续通道（多视图）通常要保留
    TargetBufferFlags discardStart = params.flags.discardStart;
    if (rt->IsSwapChain()) {
        auto sc = mCurrentSwapChain;
        LOG_ASSERT(sc);
        if (sc->IsFirstRenderPass()) {
            if (!acquireNextSwapchainImage()) {
                // 取图失败：后续调用不能假定渲染通道存在
                mCurrentRenderPass = {};
                return;
            }
            discardStart = discardStart | TargetBufferFlags::COLOR;
            sc->MarkFirstRenderPass();
        }
    }

    VulkanCommandBuffer* commandBuffer = &m_commands.Get();

    // 尺寸必须在 acquireNextSwapchainImage() 之后取，否则交换链路径下还是 0
    VkExtent2D const extent = rt->GetExtent();

    LOG_ASSERT(rt.Get() == mDefaultRenderTarget.Get() || (extent.width > 0 && extent.height > 0));

    VkCommandBuffer const cmdbuffer = commandBuffer->Buffer();

    // 裁剪矩形随每个渲染通道重置，顺带满足 VUID-vkCmdDrawIndexed-None-07832
    VkRect2D const scissor{ .offset = { 0, 0 }, .extent = extent };
    vkCmdSetScissor(cmdbuffer, 0, 1, &scissor);

    VulkanLayout      currentDepthStencilLayout = VulkanLayout::UNDEFINED;
    TargetBufferFlags clearVal                  = params.flags.clear;
    TargetBufferFlags discardEndVal             = params.flags.discardEnd;
    if (rt->HasDepthStencil()) {
        if (params.readOnlyDepthStencil & RenderPassParams::READONLY_DEPTH) {
            discardEndVal = discardEndVal & ~TargetBufferFlags::DEPTH;
            clearVal      = clearVal & ~TargetBufferFlags::DEPTH;
        }
        if (params.readOnlyDepthStencil & RenderPassParams::READONLY_STENCIL) {
            discardEndVal = discardEndVal & ~TargetBufferFlags::STENCIL;
            clearVal      = clearVal & ~TargetBufferFlags::STENCIL;
        }
        currentDepthStencilLayout = VulkanLayout::DEPTH_STENCIL_ATTACHMENT;
    }

    VulkanFboCache::RenderPassKey rpkey = rt->GetRenderPassKey();
    rpkey.clear                         = clearVal;
    rpkey.discardStart                  = discardStart;
    rpkey.discardEnd                    = discardEndVal;
    rpkey.initialDepthStencilLayout     = currentDepthStencilLayout;
    rpkey.subpassMask                   = static_cast<uint8_t>(params.subpassMask);

    VulkanRenderPassPtr renderPass = m_framebufferCache.GetRenderPass(rpkey, m_resMgr);
    m_pipelineCache.BindRenderPass(renderPass, 0);

    VulkanFboCache::FboKey fbkey = rt->GetFboKey();
    fbkey.renderPass             = renderPass->GetVkRenderPass();
    fbkey.layers                 = 1;

    rt->EmitBarriersBeginRenderPass(*commandBuffer);

    VulkanFramebufferPtr vkfb = m_framebufferCache.GetFramebuffer(fbkey, m_resMgr, rt);

    // 当前命令缓冲自此引用渲染目标与其附件，持有引用直到提交完成
    commandBuffer->Acquire(rt);
    commandBuffer->Acquire(renderPass);
    commandBuffer->Acquire(vkfb);

    VkRenderPassBeginInfo renderPassInfo{
        .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass  = renderPass->GetVkRenderPass(),
        .framebuffer = vkfb->GetVkFramebuffer(),
        // renderArea 约束 LoadOp，而裁剪不影响它，故这里只给整块区域
        .renderArea = { .offset = {}, .extent = extent },
    };

    rt->TransformClientRectToPlatform(&renderPassInfo.renderArea);

    VkClearValue clearValues[MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT + MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT + 1] = {};
    if (clearVal != TargetBufferFlags::NONE) {
        // clearValues 的顺序必须与 GetFramebuffer 里的附件顺序一致，且与是否真的清空无关
        uint32_t colorIdx = 0;
        for (int i = 0; i < MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT; i++) {
            if (fbkey.color[i]) {
                VkClearValue& clearValue = clearValues[renderPassInfo.clearValueCount++];
                // VkClearColorValue 是 union，须按附件格式选分支写入，写错会静默产生错误颜色
                switch (rt->GetColorClearKind(colorIdx++)) {
                    case VulkanRenderTarget::ColorClearKind::Float:
                        clearValue.color.float32[0] = static_cast<float>(params.clearColor.x);
                        clearValue.color.float32[1] = static_cast<float>(params.clearColor.y);
                        clearValue.color.float32[2] = static_cast<float>(params.clearColor.z);
                        clearValue.color.float32[3] = static_cast<float>(params.clearColor.w);
                        break;
                    case VulkanRenderTarget::ColorClearKind::SignedInt:
                        clearValue.color.int32[0] = static_cast<int32_t>(params.clearColor.x);
                        clearValue.color.int32[1] = static_cast<int32_t>(params.clearColor.y);
                        clearValue.color.int32[2] = static_cast<int32_t>(params.clearColor.z);
                        clearValue.color.int32[3] = static_cast<int32_t>(params.clearColor.w);
                        break;
                    case VulkanRenderTarget::ColorClearKind::UnsignedInt:
                        clearValue.color.uint32[0] = static_cast<uint32_t>(params.clearColor.x);
                        clearValue.color.uint32[1] = static_cast<uint32_t>(params.clearColor.y);
                        clearValue.color.uint32[2] = static_cast<uint32_t>(params.clearColor.z);
                        clearValue.color.uint32[3] = static_cast<uint32_t>(params.clearColor.w);
                        break;
                }
            }
        }
        // 解析附件不参与清空但占位，需要跳过
        for (int i = 0; i < MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT; i++) {
            if (rpkey.needsResolveMask & (1u << i)) {
                renderPassInfo.clearValueCount++;
            }
        }
        if (fbkey.depthStencil) {
            VkClearValue& clearValue = clearValues[renderPassInfo.clearValueCount++];
            clearValue.depthStencil  = { static_cast<float>(params.clearDepth), params.clearStencil };
        }
        renderPassInfo.pClearValues = &clearValues[0];
    }

    {
        auto const& att = rt->GetColor(0);
    }
    vkCmdBeginRenderPass(cmdbuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {
        .x        = static_cast<float>(params.viewport.left),
        .y        = static_cast<float>(params.viewport.bottom),
        .width    = static_cast<float>(params.viewport.width),
        .height   = static_cast<float>(params.viewport.height),
        .minDepth = params.depthRange.near,
        .maxDepth = params.depthRange.far,
    };

    rt->TransformViewportToPlatform(&viewport);
    vkCmdSetViewport(cmdbuffer, 0, 1, &viewport);

    mCurrentRenderPass = {
        .commandBuffer  = commandBuffer,
        .renderTarget   = rt,
        .renderPass     = renderPass,
        .params         = params,
        .currentSubpass = 0,
    };
}

void VulkanDriver::EndRenderPass(int) {
    if (skipDueToEmptyRenderPass()) {
        return;
    }

    VkCommandBuffer const cmdbuffer = mCurrentRenderPass.commandBuffer->Buffer();
    vkCmdEndRenderPass(cmdbuffer);

    auto rt = mCurrentRenderPass.renderTarget;
    LOG_ASSERT(rt);

    // 紧接着可能就采样刚写入的渲染目标，故此处必须有「写 → 读」屏障
    rt->EmitBarriersEndRenderPass(*mCurrentRenderPass.commandBuffer);

    mCurrentRenderPass.renderTarget  = {};
    mCurrentRenderPass.renderPass    = {};
    mCurrentRenderPass.commandBuffer = nullptr;
}

void VulkanDriver::NextSubpass(int) {
    if (skipDueToEmptyRenderPass()) {
        return;
    }

    LOG_ASSERT(mCurrentRenderPass.currentSubpass == 0);

    auto renderTarget = mCurrentRenderPass.renderTarget;
    LOG_ASSERT(renderTarget);
    LOG_ASSERT(mCurrentRenderPass.params.subpassMask);

    vkCmdNextSubpass(mCurrentRenderPass.commandBuffer->Buffer(), VK_SUBPASS_CONTENTS_INLINE);

    m_pipelineCache.BindRenderPass(mCurrentRenderPass.renderPass, ++mCurrentRenderPass.currentSubpass);

    if (mCurrentRenderPass.params.subpassMask & 0x1) {
        VulkanAttachment& subpassInput = renderTarget->GetColor(0);
        m_descriptorSetCache.UpdateInputAttachment({}, subpassInput);
    }
}

void VulkanDriver::BindPipeline(PipelineState const& state) {
    // 整体重置，其中最关键的是 .bindInDraw
    mPipelineState = {};

    auto&                         setLayouts = state.pipelineLayout.setLayout;
    DescriptorSetLayoutHandleList layoutHandles;
    uint8_t                       layoutCount = 0;
    std::transform(setLayouts.begin(), setLayouts.end(), layoutHandles.begin(), [&](auto const& handle) -> VulkanDescriptorSetLayoutPtr {
        if (!handle) {
            return {};
        }
        layoutCount++;
        return m_resMgr->Acquire<VulkanDescriptorSetLayout>(handle);
    });

    constexpr uint8_t                 kDescriptorSetMaskTable[4] = { 0x1, 0x3, 0x7, 0xF };
    VK_UTILS::DescriptorSetMask const descriptorSetMask(kDescriptorSetMaskTable[layoutCount]);

    if (mAppState.HasExternalSamplers()) {
        auto const haveExternalSamplers = [](auto const& layoutHandle) { return layoutHandle ? layoutHandle->HasExternalSamplers() : false; };
        if (std::any_of(layoutHandles.begin(), layoutHandles.end(), haveExternalSamplers)) {
            BindInDrawBundle bundle = {
                .pipelineState     = state,
                .dsLayoutHandles   = layoutHandles,
                .descriptorSetMask = descriptorSetMask,
            };
            mPipelineState.bindInDraw = { true, bundle };
            return;
        }
    }

    VulkanDescriptorSetLayout::DescriptorSetLayoutArray vkLayouts;
    std::transform(layoutHandles.begin(), layoutHandles.end(), vkLayouts.begin(),
                   [](auto const& layout) -> VkDescriptorSetLayout { return layout ? layout->GetVkLayout() : VK_NULL_HANDLE; });
    auto program        = m_resMgr->Acquire<VulkanProgram>(state.program);
    auto pipelineLayout = m_pipelineLayoutCache.GetLayout(vkLayouts, program);
    bindPipelineImpl(state, pipelineLayout, descriptorSetMask);
}

void VulkanDriver::bindPipelineImpl(PipelineState const& state, VkPipelineLayout pipelineLayout, VK_UTILS::DescriptorSetMask descriptorSetMask) {
    if (skipDueToEmptyRenderPass()) {
        return;
    }

    auto commands = mCurrentRenderPass.commandBuffer;
    auto vbi      = m_resMgr->Acquire<VulkanVertexBufferInfo>(state.vertexBufferInfo);

    RasterState const&   rasterState = state.rasterState;
    PolygonOffset const& depthOffset = state.polygonOffset;

    auto program = m_resMgr->Acquire<VulkanProgram>(state.program);
    commands->Acquire(program);

    auto rt = mCurrentRenderPass.renderTarget;

    // 与上游逐字段对齐：RasterState 的位域布局即契约，字段顺序与宽度都不可改
    VulkanPipelineCache::RasterState const vulkanRasterState{
        .cullMode                = VK_UTILS::GetCullMode(rasterState.culling),
        .frontFace               = VK_UTILS::GetFrontFace(rasterState.inverseFrontFaces),
        .depthBiasEnable         = (depthOffset.constant || depthOffset.slope) ? VK_TRUE : VK_FALSE,
        .blendEnable             = rasterState.HasBlending(),
        .depthWriteEnable        = rasterState.depthWrite,
        .alphaToCoverageEnable   = rasterState.alphaToCoverage,
        .srcColorBlendFactor     = VK_UTILS::GetBlendFactor(rasterState.blendFunctionSrcRGB),
        .dstColorBlendFactor     = VK_UTILS::GetBlendFactor(rasterState.blendFunctionDstRGB),
        .srcAlphaBlendFactor     = VK_UTILS::GetBlendFactor(rasterState.blendFunctionSrcAlpha),
        .dstAlphaBlendFactor     = VK_UTILS::GetBlendFactor(rasterState.blendFunctionDstAlpha),
        .colorWriteMask          = static_cast<VkColorComponentFlags>(rasterState.colorWrite ? 0xfu : 0x0u),
        .rasterizationSamples    = rt->GetSamples(),
        .depthClamp              = static_cast<uint8_t>(rasterState.depthClamp ? 1u : 0u),
        .colorTargetCount        = rt->GetColorTargetCount(mCurrentRenderPass),
        .colorBlendOp            = rasterState.blendEquationRGB,
        .alphaBlendOp            = rasterState.blendEquationAlpha,
        .depthCompareOp          = rasterState.depthFunc,
        .depthBiasConstantFactor = depthOffset.constant,
        .depthBiasSlopeFactor    = depthOffset.slope,
    };

    // Vulkan 里拓扑属于管线状态
    VkPrimitiveTopology const topology = VK_UTILS::GetPrimitiveTopology(state.primitiveType);

    VkVertexInputAttributeDescription const* attribDesc = vbi->GetAttribDescriptions();
    VkVertexInputBindingDescription const*   bufferDesc = vbi->GetBufferDescriptions();

    // 先把状态推给管线缓存（不产生 VK 调用），再由 bindPipeline 落成实际管线
    m_pipelineCache.BindProgram(program);
    m_pipelineCache.BindRasterState(vulkanRasterState);
    m_pipelineCache.BindStencilState(state.stencilState);
    m_pipelineCache.BindPrimitiveTopology(topology);
    m_pipelineCache.BindVertexArray(attribDesc, bufferDesc, vbi->GetAttributeCount());

    // 不能整体重置 mPipelineState：bindInDraw 的元数据要跨绑定保留
    mPipelineState.program           = program;
    mPipelineState.pipelineLayout    = pipelineLayout;
    mPipelineState.descriptorSetMask = descriptorSetMask;

    m_pipelineCache.BindLayout(pipelineLayout);
    m_pipelineCache.BindPipeline(mCurrentRenderPass.commandBuffer);
}

void VulkanDriver::BindRenderPrimitive(RenderPrimitiveHandle rph) {
    mRenderPrimitiveState.bound = false;
    if (skipDueToEmptyRenderPass()) {
        return;
    }

    auto prim = m_resMgr->Acquire<VulkanRenderPrimitive>(rph);

    if (!prim->vertexBuffer->IsValid()) {
        return;
    }
    // 这一个标记决定本次 draw 是否真的发出绘制调用
    mRenderPrimitiveState.bound = true;

    VulkanCommandBuffer*  commands  = mCurrentRenderPass.commandBuffer;
    VkCommandBuffer const cmdbuffer = commands->Buffer();
    commands->Acquire(prim);

    // 必须与 bindPipeline() 里绑定的 VulkanVertexBufferInfo 一致；但允许先绑图元再绑管线，
    // 故校验只能留到 draw()
    auto vbi = prim->vertexBuffer->vbi;

    uint32_t const      bufferCount = vbi->GetAttributeCount();
    VkDeviceSize const* offsets     = vbi->GetOffsets();
    VkBuffer const*     buffers     = prim->vertexBuffer->GetVkBuffers();

    // 两个绑定都是有条件的：无属性图元没有顶点缓冲，非索引图元没有索引缓冲
    if (bufferCount > 0) {
        vkCmdBindVertexBuffers(cmdbuffer, 0, bufferCount, buffers, offsets);
    }
    if (prim->indexBuffer) {
        vkCmdBindIndexBuffer(cmdbuffer, prim->indexBuffer->GetVkBuffer(), 0, prim->indexBuffer->indexType);
    }
}

void VulkanDriver::BindDescriptorSet(DescriptorSetHandle dsh, descriptor_set_t setIndex, DescriptorSetOffsetArray&& offsets) {
    if (dsh) {
        auto set = m_resMgr->Acquire<VulkanDescriptorSet>(dsh);

        m_descriptorSetCache.Bind(setIndex, set, std::move(offsets));

        if (set->isAnExternalSamplerBound) {
            auto const& bindInDrawBundle = mPipelineState.bindInDraw.second;
            // 该 set 已经被绑过，而它带外部采样器时可能在本次绑定中换掉 pipelineLayout，
            // 故需要重走一次 bindInDraw
            if (bindInDrawBundle.descriptorSetMask[setIndex]) {
                mPipelineState.bindInDraw.first = true;
            }
        }
    } else {
        m_descriptorSetCache.Unbind(setIndex);
    }
}

void VulkanDriver::prepareDraw() {
    if (skipDueToEmptyRenderPass()) {
        return;
    }

    auto const& [doBindInDraw, bundle] = mPipelineState.bindInDraw;
    if (doBindInDraw) {
        VulkanDescriptorSetCache::DescriptorSetArray const& boundSets = m_descriptorSetCache.GetBoundSets();
        VulkanDescriptorSetLayout::DescriptorSetLayoutArray vklayouts;
        for (size_t i = 0; i < boundSets.size(); i++) {
            // 描述符集的最终布局在 bindDescriptorSet 时按需重建，此处直接取当前生效的
            vklayouts[i] = boundSets[i] ? boundSets[i]->boundLayout : VK_NULL_HANDLE;
        }
        auto                   program        = m_resMgr->Acquire<VulkanProgram>(bundle.pipelineState.program);
        VkPipelineLayout const pipelineLayout = m_pipelineLayoutCache.GetLayout(vklayouts, program);
        if (pipelineLayout != mPipelineState.pipelineLayout) {
            bindPipelineImpl(bundle.pipelineState, pipelineLayout, bundle.descriptorSetMask);
            // 布局此前为空时收到的 push constant 尚未写出，此时统一冲刷
            program->FlushPushConstants(pipelineLayout);
        }
        mPipelineState.bindInDraw.first = false;
    }
    m_descriptorSetCache.Commit(mCurrentRenderPass.commandBuffer, mPipelineState.pipelineLayout, mPipelineState.descriptorSetMask);
}

void VulkanDriver::Draw2(uint32_t indexOffset, uint32_t indexCount, uint32_t instanceCount) {
    // 没有绑定渲染图元时不画
    if (!mRenderPrimitiveState.bound) {
        return;
    }

    prepareDraw();

    constexpr int32_t     vertexOffset = 0;
    constexpr uint32_t    firstInstId  = 0;
    VkCommandBuffer const cmdbuffer    = mCurrentRenderPass.commandBuffer->Buffer();

    vkCmdDrawIndexed(cmdbuffer, indexCount, instanceCount, indexOffset, vertexOffset, firstInstId);
}

void VulkanDriver::DrawArrays(uint32_t vertexOffset, uint32_t vertexCount, uint32_t instanceCount) {
    prepareDraw();

    constexpr uint32_t    firstInstId = 0;
    VkCommandBuffer const cmdbuffer   = mCurrentRenderPass.commandBuffer->Buffer();

    vkCmdDraw(cmdbuffer, vertexCount, instanceCount, vertexOffset, firstInstId);
}

void VulkanDriver::Draw(PipelineState state, RenderPrimitiveHandle rph, uint32_t indexOffset, uint32_t indexCount, uint32_t instanceCount) {
    // 图元类型与顶点缓冲信息由图元决定，调用方传入的不可信
    auto rp                = m_resMgr->Acquire<VulkanRenderPrimitive>(rph);
    state.primitiveType    = rp->type;
    state.vertexBufferInfo = VertexBufferInfoHandle(rp->vertexBuffer->vbi->GetId());
    BindPipeline(state);
    BindRenderPrimitive(rph);
    Draw2(indexOffset, indexCount, instanceCount);
}

void VulkanDriver::DispatchCompute(ProgramHandle program, ::math::uint3 workGroupCount) {
    // 上游即为此处的空实现
}

void VulkanDriver::Scissor(Viewport scissorBox) {
    if (skipDueToEmptyRenderPass()) {
        return;
    }

    VkCommandBuffer const cmdbuffer = mCurrentRenderPass.commandBuffer->Buffer();

    // 左下角夹到 0 并避免溢出
    constexpr int32_t  maxvali = std::numeric_limits<int32_t>::max();
    constexpr uint32_t maxvalu = std::numeric_limits<int32_t>::max();
    int32_t            l       = scissorBox.left;
    int32_t            b       = scissorBox.bottom;
    uint32_t           w       = std::min(maxvalu, scissorBox.width);
    uint32_t           h       = std::min(maxvalu, scissorBox.height);
    int32_t            r       = (l > int32_t(maxvalu - w)) ? maxvali : l + int32_t(w);
    int32_t            t       = (b > int32_t(maxvalu - h)) ? maxvali : b + int32_t(h);
    l                          = std::max(0, l);
    b                          = std::max(0, b);
    LOG_ASSERT(r >= l && t >= b);
    VkRect2D scissor{
        .offset = { l, b },
        .extent = { static_cast<uint32_t>(r - l), static_cast<uint32_t>(t - b) },
    };

    auto rt = mCurrentRenderPass.renderTarget;
    rt->TransformClientRectToPlatform(&scissor);
    vkCmdSetScissor(cmdbuffer, 0, 1, &scissor);
}

void VulkanDriver::MakeCurrent(SwapChainHandle drawSch, SwapChainHandle readSch) {
    LOG_ASSERT(drawSch == readSch && "Vulkan driver does not support distinct draw/read swap chains.");

    // 渲染新交换链前，默认渲染目标必须先放掉旧的交换链图像
    mDefaultRenderTarget->ReleaseSwapchain();

    mCurrentSwapChain = m_resMgr->Acquire<VulkanSwapChain>(drawSch);
}

void VulkanDriver::Commit(SwapChainHandle sch) {
    auto swapChain = m_resMgr->Acquire<VulkanSwapChain>(sch);

    // 等最近一次提交完成后再呈现后备缓冲
    swapChain->Present();
}

void VulkanDriver::SetPushConstant(ShaderStage stage, uint8_t index, PushConstantVariant value) {
    if (skipDueToEmptyRenderPass()) {
        return;
    }
    LOG_ASSERT(mPipelineState.program && "Expect a program when writing to push constants");
    LOG_ASSERT(mCurrentRenderPass.commandBuffer && "Should be called within a renderpass");
    mPipelineState.program->WritePushConstant(mCurrentRenderPass.commandBuffer->Buffer(), mPipelineState.pipelineLayout, stage, index, value);
}

bool VulkanDriver::acquireNextSwapchainImage() {
    // 未 makeCurrent 时没有交换链：上游用 assert 拦，这里让渲染通道退化为空
    if (!mCurrentSwapChain) {
        return false;
    }
    LOG_ASSERT(mDefaultRenderTarget);

    // 已经绑过交换链图像：直接成功
    if (mDefaultRenderTarget->IsSwapchainBound()) {
        return true;
    }

    auto const [acquired, backingChanged] = mCurrentSwapChain->Acquire();
    if (backingChanged) {
        m_framebufferCache.ResetFramebuffers();
    }

    if (acquired) {
        // 顺序不可颠倒：bindSwapChain 会读取上面刚取回的图像
        mDefaultRenderTarget->BindSwapChain(mCurrentSwapChain);
        return true;
    }
    // 取图失败：渲染目标不再可用
    mDefaultRenderTarget->ReleaseSwapchain();
    return false;
}

// ---------------------------------------------------------------- 特性查询

// 这些返回值决定前端选择哪条渲染路径，故逐条对照上游实现，不留占位返回
bool VulkanDriver::IsTextureFormatSupported(TextureFormat format) {
    VkFormat const vkformat = VK_UTILS::GetVkFormat(format);
    if (vkformat == VK_FORMAT_UNDEFINED) {
        return false;
    }
    VkFormatProperties info;
    vkGetPhysicalDeviceFormatProperties(mPlatform->GetVkPhysicalDevice(), vkformat, &info);
    // 纹理一律按 VK_IMAGE_TILING_OPTIMAL 创建，故以该 tiling 的可用性为准
    return info.optimalTilingFeatures != 0;
}

bool VulkanDriver::IsTextureSwizzleSupported() { return true; }

bool VulkanDriver::IsTextureFormatMipmappable(TextureFormat format) {
    switch (format) {
        case TextureFormat::DEPTH16:
        case TextureFormat::DEPTH24:
        case TextureFormat::DEPTH32F:
        case TextureFormat::DEPTH24_STENCIL8:
        case TextureFormat::DEPTH32F_STENCIL8:
            return false;
        default:
            return IsRenderTargetFormatSupported(format);
    }
}

bool VulkanDriver::IsTextureFormatFilterable(TextureFormat format) {
    VkFormat const vkformat = VK_UTILS::GetVkFormat(format);
    if (vkformat == VK_FORMAT_UNDEFINED) {
        return false;
    }
    VkFormatProperties info;
    vkGetPhysicalDeviceFormatProperties(mPlatform->GetVkPhysicalDevice(), vkformat, &info);
    return (info.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
}

bool VulkanDriver::IsRenderTargetFormatSupported(TextureFormat format) {
    VkFormat const vkformat = VK_UTILS::GetVkFormat(format);
    if (vkformat == VK_FORMAT_UNDEFINED) {
        return false;
    }
    VkFormatProperties info;
    vkGetPhysicalDeviceFormatProperties(mPlatform->GetVkPhysicalDevice(), vkformat, &info);
    return (info.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0;
}

bool VulkanDriver::IsFrameBufferFetchSupported() { return false; }

bool VulkanDriver::IsFrameBufferFetchMultiSampleSupported() { return false; }

bool VulkanDriver::IsFrameTimeSupported() { return true; }

bool VulkanDriver::IsAutoDepthResolveSupported() { return false; }

bool VulkanDriver::IsSRGBSwapChainSupported() { return mIsSRGBSwapChainSupported; }

bool VulkanDriver::IsMSAASwapChainSupported(uint32_t samples) {
    // MSAA 交换链上游同样是硬编码关闭
    return false;
}

bool VulkanDriver::IsProtectedContentSupported() { return m_context->IsProtectedMemorySupported(); }

bool VulkanDriver::IsProtectedTexturesSupported() { return IsProtectedContentSupported(); }

bool VulkanDriver::IsStereoSupported() {
    switch (mStereoscopicType) {
        case StereoscopicType::Instanced:
            return m_context->IsClipDistanceSupported();
        case StereoscopicType::Multiview:
            return m_context->IsMultiviewEnabled();
        case StereoscopicType::None:
            return false;
    }
    return false;
}

bool VulkanDriver::IsParallelShaderCompileSupported() { return m_context->IsPipelineCachePrewarmingEnabled(); }

bool VulkanDriver::IsDepthStencilResolveSupported() { return false; }

bool VulkanDriver::IsDepthStencilBlitSupported(TextureFormat format) {
    auto const& formats = m_context->GetBlittableDepthStencilFormats();
    return std::find(formats.begin(), formats.end(), VK_UTILS::GetVkFormat(format)) != formats.end();
}

bool VulkanDriver::IsDepthClampSupported() { return m_context->IsDepthClampSupported(); }

bool VulkanDriver::IsAsynchronousModeEnabled() { return false; }

bool VulkanDriver::IsWorkaroundNeeded(Workaround workaround) {
    switch (workaround) {
        case Workaround::SplitEasu: {
            // EASU 的提前退出条件在着色器里被展平，仅高通 GPU 需要拆分
            return m_context->GetPhysicalDeviceVendorId() == 0x5143;
        }
        case Workaround::AllowReadOnlyAncillaryFeedbackLoop:
        case Workaround::AdrenoUniformArrayCrash:
        case Workaround::DisableBlitIntoTextureArray:
            return false;
        default:
            return false;
    }
}

FeatureLevel VulkanDriver::GetFeatureLevel() {
    VkPhysicalDeviceLimits const& limits = m_context->GetPhysicalDeviceLimits();

    // 不支持立方体贴图数组即只能算 FL1
    if (!m_context->IsImageCubeArraySupported()) {
        return FeatureLevel::FEATURE_LEVEL_1;
    }

    // 采样器数量达不到 FL2 标准即为 FL1
    auto const& fl2 = kFeatureLevelCaps[static_cast<size_t>(FeatureLevel::FEATURE_LEVEL_2)];
    if (limits.maxPerStageDescriptorSamplers < fl2.maxVertexSamplerCount || limits.maxPerStageDescriptorSamplers < fl2.maxFragmentSamplerCount) {
        return FeatureLevel::FEATURE_LEVEL_1;
    }

    // 采样器数量达不到 FL3 标准即为 FL2
    auto const& fl3 = kFeatureLevelCaps[static_cast<size_t>(FeatureLevel::FEATURE_LEVEL_3)];
    if (limits.maxPerStageDescriptorSamplers < fl3.maxVertexSamplerCount || limits.maxPerStageDescriptorSamplers < fl3.maxFragmentSamplerCount) {
        return FeatureLevel::FEATURE_LEVEL_2;
    }

    return FeatureLevel::FEATURE_LEVEL_3;
}

::math::float2 VulkanDriver::GetClipSpaceParams() {
    // Vulkan 的裁剪空间 z 落在 [0, w]，与 GL 的 [-w, w] 不同，故取 {1, 0}
    return ::math::float2{ 1.0f, 0.0f };
}

uint8_t VulkanDriver::GetMaxDrawBuffers() { return m_context->GetPhysicalDeviceLimits().maxColorAttachments; }

size_t VulkanDriver::GetMaxUniformBufferSize() {
    return std::max(m_context->GetPhysicalDeviceLimits().maxUniformBufferRange,
                    static_cast<uint32_t>(m_context->GetPhysicalDeviceLimits().nonCoherentAtomSize));
}

size_t VulkanDriver::GetMaxTextureSize(SamplerType target) {
    switch (target) {
        case SamplerType::SAMPLER_2D:
            return m_context->GetPhysicalDeviceLimits().maxImageDimension2D;
        case SamplerType::SAMPLER_3D:
            return m_context->GetPhysicalDeviceLimits().maxImageDimension3D;
        case SamplerType::SAMPLER_CUBEMAP:
            return m_context->GetPhysicalDeviceLimits().maxImageDimensionCube;
        default:
            return m_context->GetPhysicalDeviceLimits().maxImageDimension1D;
    }
}

size_t VulkanDriver::GetMaxArrayTextureLayers() { return m_context->GetPhysicalDeviceLimits().maxImageArrayLayers; }

size_t VulkanDriver::GetUniformBufferOffsetAlignment() { return m_context->GetPhysicalDeviceLimits().minUniformBufferOffsetAlignment; }

bool VulkanDriver::IsCompositorTimingSupported() { return mPlatform->IsCompositorTimingSupported(); }

bool VulkanDriver::QueryFrameTimestamps(SwapChainHandle swapChain, uint64_t frameId, Platform::FrameTimestamps* outFrameTimestamps) {
    // 同步调用：句柄为空或不是本驱动创建的交换链都直接失败
    if (!swapChain) {
        return false;
    }
    std::lock_guard const lock(mTiming.lock);
    // 交换链须由本驱动创建；headless 路径无原生窗口，故平台侧同样返回 false
    return mTiming.nativeSwapchains.find(swapChain.GetId()) != mTiming.nativeSwapchains.end() &&
           mPlatform->QueryFrameTimestamps(nullptr, frameId, outFrameTimestamps);
}

bool VulkanDriver::QueryCompositorTiming(SwapChainHandle swapChain, Platform::CompositorTiming* outCompositorTiming) {
    if (!swapChain) {
        return false;
    }
    std::lock_guard const lock(mTiming.lock);
    return mTiming.nativeSwapchains.find(swapChain.GetId()) != mTiming.nativeSwapchains.end() &&
           mPlatform->QueryCompositorTiming(nullptr, outCompositorTiming);
}

// ---------------------------------------------------------------- 调试、捕获与空桩

void VulkanDriver::InsertEventMarker(const char* string) { m_commands.InsertEventMarker(string); }

void VulkanDriver::PushGroupMarker(const char* string) { m_commands.PushGroupMarker(string); }

void VulkanDriver::PopGroupMarker(int) { m_commands.PopGroupMarker(); }

void VulkanDriver::StartCapture(int) {}

void VulkanDriver::StopCapture(int) {}

AsyncCallId VulkanDriver::QueueCommandAsyncS() noexcept {
    // 上游同样未实现
    return 0;
}

void VulkanDriver::QueueCommandAsyncR(AsyncCallId jobId, std::function<void()>&& command, CallbackHandler* handler,
                                      CallbackHandler::Callback callback, void* user) {}

bool VulkanDriver::CancelAsyncJob(AsyncCallId jobId) {
    // 上游同样未实现
    return false;
}

Handle<HwStream> VulkanDriver::CreateStreamNative(void* stream, NS_UTILS::ImmutableString tag) {
    // 上游同样只打日志并返回空句柄
    LOG_WARN("CreateStreamNative 未实现：视频流路径已按设计砍掉");
    return {};
}

Handle<HwStream> VulkanDriver::CreateStreamAcquired(NS_UTILS::ImmutableString tag) {
    LOG_WARN("CreateStreamAcquired 未实现：视频流路径已按设计砍掉");
    return {};
}

void VulkanDriver::SetAcquiredImage(StreamHandle stream, void* image, const ::math::mat3f& transform, CallbackHandler* handler, StreamCallback cb,
                                    void* userData) {
    LOG_WARN("SetAcquiredImage 未实现：视频流路径已按设计砍掉");
}

void VulkanDriver::SetStreamDimensions(StreamHandle stream, uint32_t width, uint32_t height) {
    LOG_WARN("SetStreamDimensions 未实现：视频流路径已按设计砍掉");
}

int64_t VulkanDriver::GetStreamTimestamp(StreamHandle stream) { return 0; }

void VulkanDriver::UpdateStreams(DriverApi* driver) { LOG_WARN("UpdateStreams 未实现：视频流路径已按设计砍掉"); }

void VulkanDriver::DestroyStream(StreamHandle sh) {
    if (!sh) {
        return;
    }
    LOG_WARN("DestroyStream 未实现：视频流路径已按设计砍掉");
}

void VulkanDriver::SetExternalStream(TextureHandle th, StreamHandle sh) { LOG_WARN("SetExternalStream 未实现：视频流路径已按设计砍掉"); }

// ---------------------------------------------------------------- 读回与 blit

void VulkanDriver::ReadPixels(RenderTargetHandle src, uint32_t x, uint32_t y, uint32_t width, uint32_t height, PixelBufferDescriptor&& pbd) {
    auto srcTarget = m_resMgr->Acquire<VulkanRenderTarget>(src);
    endCommandRecording();
    m_readPixels.Run(
        srcTarget, x, y, width, height, mPlatform->GetGraphicsQueueFamilyIndex(), std::move(pbd),
        [&context = m_context](uint32_t types, VkFlags reqs) { return context->SelectMemoryType(types, reqs); },
        [this](PixelBufferDescriptor&& data) { deferDestroy(std::move(data)); });
}

void VulkanDriver::ReadTexture(TextureHandle src, uint8_t level, uint16_t layer, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                               PixelBufferDescriptor&& pbd) {
    auto srcTexture = m_resMgr->Acquire<VulkanTexture>(src);

    // pbd 不支持 3D 纹理，故此处直接拒绝
    LOG_ASSERT(srcTexture->target != SamplerType::SAMPLER_3D);

    endCommandRecording();
    m_readPixels.Run(
        srcTexture, level, layer, x, y, width, height, mPlatform->GetGraphicsQueueFamilyIndex(), std::move(pbd),
        [&context = m_context](uint32_t types, VkFlags reqs) { return context->SelectMemoryType(types, reqs); },
        [this](PixelBufferDescriptor&& data) { deferDestroy(std::move(data)); });
}

void VulkanDriver::ReadBufferSubData(BufferObjectHandle src, uint32_t offset, uint32_t size, BufferDescriptor&& data) {
    // 上游同样未实现，只把数据交回给调用方释放
    deferDestroy(std::move(data));
}

void VulkanDriver::Resolve(TextureHandle dst, uint8_t dstLevel, uint8_t dstLayer, TextureHandle src, uint8_t srcLevel, uint8_t srcLayer) {
    LOG_ASSERT(!mCurrentRenderPass.renderPass && "Resolve() cannot be invoked inside a render pass.");

    auto srcTexture = m_resMgr->Acquire<VulkanTexture>(src);
    auto dstTexture = m_resMgr->Acquire<VulkanTexture>(dst);

    LOG_ASSERT(srcTexture);
    LOG_ASSERT(dstTexture);
    LOG_ASSERT(dstTexture->width == srcTexture->width && dstTexture->height == srcTexture->height);
    LOG_ASSERT(srcTexture->samples > 1 && dstTexture->samples == 1);
    LOG_ASSERT(srcTexture->format == dstTexture->format);
    LOG_ASSERT(!VK_UTILS::IsVkDepthFormat(VK_UTILS::GetVkFormat(srcTexture->format)));
    LOG_ASSERT(!VK_UTILS::IsVkStencilFormat(VK_UTILS::GetVkFormat(srcTexture->format)));
    LOG_ASSERT(HasAnyFlag(dstTexture->usage, TextureUsage::BLIT_DST));
    LOG_ASSERT(HasAnyFlag(srcTexture->usage, TextureUsage::BLIT_SRC));

    m_blitter.Resolve({ .texture = dstTexture, .level = dstLevel, .layer = dstLayer },
                      { .texture = srcTexture, .level = srcLevel, .layer = srcLayer });
}

void VulkanDriver::Blit(TextureHandle dst, uint8_t srcLevel, uint8_t srcLayer, ::math::uint2 dstOrigin, TextureHandle src, uint8_t dstLevel,
                        uint8_t dstLayer, ::math::uint2 srcOrigin, ::math::uint2 size) {
    LOG_ASSERT(!mCurrentRenderPass.renderPass && "Blit() cannot be invoked inside a render pass.");

    auto srcTexture = m_resMgr->Acquire<VulkanTexture>(src);
    auto dstTexture = m_resMgr->Acquire<VulkanTexture>(dst);

    LOG_ASSERT(HasAnyFlag(dstTexture->usage, TextureUsage::BLIT_DST));
    LOG_ASSERT(HasAnyFlag(srcTexture->usage, TextureUsage::BLIT_SRC));
    LOG_ASSERT(srcTexture->format == dstTexture->format);

    // 下面的 Y 反转让 Vk 的坐标系与 GL / Metal 对齐
    auto const       srcLeft       = static_cast<int32_t>(srcOrigin.x);
    auto const       dstLeft       = static_cast<int32_t>(dstOrigin.x);
    auto const       srcTop        = static_cast<int32_t>(srcTexture->height - (srcOrigin.y + size.y));
    auto const       dstTop        = static_cast<int32_t>(dstTexture->height - (dstOrigin.y + size.y));
    auto const       srcRight      = static_cast<int32_t>(srcOrigin.x + size.x);
    auto const       dstRight      = static_cast<int32_t>(dstOrigin.x + size.x);
    auto const       srcBottom     = static_cast<int32_t>(srcTop + size.y);
    auto const       dstBottom     = static_cast<int32_t>(dstTop + size.y);
    VkOffset3D const srcOffsets[2] = { { srcLeft, srcTop, 0 }, { srcRight, srcBottom, 1 } };
    VkOffset3D const dstOffsets[2] = { { dstLeft, dstTop, 0 }, { dstRight, dstBottom, 1 } };

    // 不保证缩放
    m_blitter.Blit(VK_FILTER_NEAREST, { .texture = dstTexture, .level = dstLevel, .layer = dstLayer }, dstOffsets,
                   { .texture = srcTexture, .level = srcLevel, .layer = srcLayer }, srcOffsets);
}

void VulkanDriver::BlitDEPRECATED(TargetBufferFlags buffers, RenderTargetHandle dst, Viewport dstRect, RenderTargetHandle src, Viewport srcRect,
                                  SamplerMagFilter filter) {
    LOG_ASSERT(!mCurrentRenderPass.renderPass && "BlitDEPRECATED() cannot be invoked inside a render pass.");
    LOG_ASSERT(buffers == TargetBufferFlags::COLOR0 && "blitDEPRECATED only supports COLOR0");
    LOG_ASSERT(srcRect.left >= 0 && srcRect.bottom >= 0 && dstRect.left >= 0 && dstRect.bottom >= 0 &&
               "Source and destination rects must be positive.");

    auto dstTarget = m_resMgr->Acquire<VulkanRenderTarget>(dst);
    auto srcTarget = m_resMgr->Acquire<VulkanRenderTarget>(src);

    // 合法用法：需要保证交换链图像已取得并绑到默认渲染目标；重复取得是安全的
    if (dstTarget.Get() == mDefaultRenderTarget.Get()) {
        acquireNextSwapchainImage();
    }

    VkFilter const vkfilter = (filter == SamplerMagFilter::Nearest) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;

    // 下面的 Y 反转让 Vk 的坐标系与 GL / Metal 对齐
    VkExtent2D const srcExtent = srcTarget->GetExtent();
    VkExtent2D const dstExtent = dstTarget->GetExtent();

    auto const       dstLeft       = static_cast<int32_t>(dstRect.left);
    auto const       srcLeft       = static_cast<int32_t>(srcRect.left);
    auto const       dstTop        = static_cast<int32_t>(dstExtent.height - (dstRect.bottom + dstRect.height));
    auto const       srcTop        = static_cast<int32_t>(srcExtent.height - (srcRect.bottom + srcRect.height));
    auto const       dstRight      = static_cast<int32_t>(dstRect.left + dstRect.width);
    auto const       srcRight      = static_cast<int32_t>(srcRect.left + srcRect.width);
    auto const       dstBottom     = static_cast<int32_t>(dstTop + dstRect.height);
    auto const       srcBottom     = static_cast<int32_t>(srcTop + srcRect.height);
    VkOffset3D const srcOffsets[2] = { { srcLeft, srcTop, 0 }, { srcRight, srcBottom, 1 } };
    VkOffset3D const dstOffsets[2] = { { dstLeft, dstTop, 0 }, { dstRight, dstBottom, 1 } };

    auto const& dstAttachment = dstTarget->GetColor(0);
    auto const& srcAttachment = srcTarget->GetColor(0);

    if (srcAttachment.texture->samples > 1) {
        m_blitter.Resolve(dstAttachment, srcAttachment);
    } else {
        m_blitter.Blit(vkfilter, dstAttachment, dstOffsets, srcAttachment, srcOffsets);
    }
}

void VulkanDriver::terminate() { DestroyResources(); }

template class ConcreteDispatcher<VulkanDriver>;

END_NS_BACKEND
