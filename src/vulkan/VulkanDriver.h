#pragma once

#include "Backend/BufferDescriptor.h"
#include "Backend/Driver.h"
#include "Backend/DriverDefine.h"
#include "Backend/PixelBufferDescriptor.h"
#include "Backend/platform/Platform.h"
#include "Backend/platform/VulkanPlatform.h"

#include "Utils/Utils.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <variant>

#include "DriverBase.h"
#include "VulkanBlitter.h"
#include "VulkanConstants.h"
#include "VulkanContext.h"
#include "VulkanDescriptorSetCache.h"
#include "VulkanDescriptorSetLayoutCache.h"
#include "VulkanFboCache.h"
#include "VulkanMemory.h"
#include "VulkanPipelineCache.h"
#include "VulkanPipelineLayoutCache.h"
#include "VulkanQueryManager.h"
#include "VulkanReadPixels.h"
#include "VulkanSamplerCache.h"
#include "VulkanTexture.h"
#include "VulkanYcbcrConversionCache.h"
#include "buffer/VulkanBufferCache.h"
#include "commands/VulkanCommands.h"
#include "resource/ResourceManager.h"
#include "stage/VulkanStagePool.h"
#include "sync/VulkanSemaphoreManager.h"

#undef DECL_DRIVER_API
#undef DECL_DRIVER_API_SYNCHRONOUS
#undef DECL_DRIVER_API_RETURN
BEGIN_NS_BACKEND

class VulkanDriver final : public DriverBase {
    using DescriptorSetLayoutHandleList =
        std::array<VulkanDescriptorSetLayoutPtr, VulkanDescriptorSetLayout::kUniqueDescriptorSetCount>;

    struct TimingState {
        std::mutex                                                      lock;
        std::unordered_map<HandleBase::HandleId, Platform::SwapChain *> nativeSwapchains;
    };

    struct BindInDrawBundle {
        PipelineState                 pipelineState     = {};
        DescriptorSetLayoutHandleList dsLayoutHandles   = {};
        VK_UTILS::DescriptorSetMask   descriptorSetMask = {};
        VulkanProgramPtr              program           = {};
    };

    struct PipelineBindingState {
        VulkanProgramPtr program = {};  // push constant 的写入目标
        // draw() 中提交动态 ubo 时使用
        VkPipelineLayout            pipelineLayout    = VK_NULL_HANDLE;
        VK_UTILS::DescriptorSetMask descriptorSetMask = {};

        std::pair<bool, BindInDrawBundle> bindInDraw = { false, {} };
    };

    struct AppState {
        // 应用是否已把外部采样器绑到描述符集上：一旦发生，bindPipeline 必须走慢路径
        bool hasExternalSamplerLayouts = false;
        bool hasBoundExternalImages    = false;

        NODISCARD bool HasExternalSamplers() const noexcept {
            return hasExternalSamplerLayouts && hasBoundExternalImages;
        }
    };

    struct RenderPrimitiveState {
        bool bound = false;
    };

    struct PendingDescriptor {
        std::variant<BufferDescriptor, PixelBufferDescriptor> data;
        uint32_t                                              submittedAge;
    };

public:
    VulkanDriver(const VulkanPlatformPtr &platform, const VulkanContextPtr &context, const DriverConfig &config);
    ~VulkanDriver() noexcept override;

    static DriverPtr Create(const VulkanPlatformPtr &platform, const VulkanContextPtr &context,
                            const DriverConfig &config);

    Dispatcher GetDispatcher() const noexcept override;

    template <typename T>
    friend class ConcreteDispatcher;

#define DECL_DRIVER_API(methodName, paramsDecl, params)                      inline void methodName(paramsDecl);
#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params) RetType methodName(paramsDecl) override;
#define DECL_DRIVER_API_RETURN(RetType, methodName, paramsDecl, params) \
    RetType methodName##S() noexcept override;                          \
    inline void methodName##R(RetType, paramsDecl);

#include "Backend/DriverAPI.inc"

private:
    void DebugCommandBegin(CommandStream *cmds, bool synchronous, char const *methodName) noexcept override;

    void DestroyResources() noexcept;

    void collectGarbage();
    void bindPipelineImpl(PipelineState const &pipelineState, VkPipelineLayout pipelineLayout,
                          VK_UTILS::DescriptorSetMask descriptorSetMask);

    // 索引与非索引绘制共用的前置步骤：处理 bindInDraw 的延迟布局绑定并提交描述符集
    void prepareDraw();

    void endCommandRecording();

    // 返回是否成功取得下一张交换链图像
    NODISCARD bool acquireNextSwapchainImage();

    // BufferDescriptor / PixelBufferDescriptor 的析构会触发应用回调，必须延后到在途命令
    // 都完成之后再执行，否则应用释放的内存可能仍被 GPU 读取
    void deferDestroy(BufferDescriptor &&data);
    void deferDestroy(PixelBufferDescriptor &&data);
    void collectDescriptors();

    NODISCARD bool skipDueToEmptyRenderPass() const { return !bool(mCurrentRenderPass.renderTarget); }

private:
    VulkanPlatformPtr                 mPlatform;
    ResourceManagerPtr                m_resMgr;
    VulkanSwapChainPtr                mCurrentSwapChain;
    VulkanRenderTargetPtr             mDefaultRenderTarget;
    VulkanRenderPassContext           mCurrentRenderPass = {};
    VmaAllocator                      m_allocator        = VK_NULL_HANDLE;
    VulkanContextPtr                  m_context;
    VulkanSemaphoreManagerPtr         m_semaphoreManager;
    VulkanCommandsPtr                 m_commands;
    VulkanPipelineLayoutCachePtr      m_pipelineLayoutCache;
    VulkanPipelineCachePtr            m_pipelineCache;
    VulkanStagePoolPtr                m_stagePool;
    VulkanBufferCachePtr              m_bufferCache;
    VulkanFboCachePtr                 m_framebufferCache;
    VulkanYcbcrConversionCachePtr     m_ycbcrConversionCache;
    VulkanSamplerCachePtr             m_samplerCache;
    VulkanBlitterPtr                  m_blitter;
    VulkanReadPixelsPtr               m_readPixels;
    VulkanDescriptorSetLayoutCachePtr m_descriptorSetLayoutCache;
    VulkanDescriptorSetCachePtr       m_descriptorSetCache;
    VulkanQueryManagerPtr             m_queryManager;
    TimingState                       mTiming;
    PipelineBindingState              mPipelineState{};
    AppState                          mAppState{};
    RenderPrimitiveState              mRenderPrimitiveState{};
    bool const                        mIsSRGBSwapChainSupported;
    StereoscopicType const            mStereoscopicType;
    uint8_t const                     mStereoscopicEyeCount;
    AsynchronousMode const            mAsynchronousMode;
    uint8_t                           m_ticksSinceLastGc = 0;
    std::deque<PendingDescriptor>     m_pendingDescriptors;
};

END_NS_BACKEND
