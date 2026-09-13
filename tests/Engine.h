#pragma once

#include "Backend/DriverDefine.h"
#include "Backend/PlatformFactory.h"
#include "Backend/platform/VulkanPlatform.h"

#include "Utils/Utils.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>

#include "Macro.h"
#include "command/CommandBufferQueue.h"
#include "command/CommandStream.h"

namespace Backend {
class DriverBase;
}

BEGIN_NS_TEST
DECLARE_CLASS_AND_SHARE_PTR(Engine);

class Engine : public NS_UTILS::Ref {
    struct BuilderDetails;

public:
    class Builder : public NS_UTILS::BuilderBase<BuilderDetails> {
        friend struct Engine::BuilderDetails;

    public:
        Builder() noexcept;
        ~Builder() noexcept;
        Builder&  BackendType(Backend::BackendType type);
        Builder&  DriverConfig(Backend::DriverConfig const& config);
        Builder&  BufferSize(size_t requiredSize, size_t bufferSize);
        EnginePtr Build();
    };

    void                 BeginFrame(int64_t monotonicClockNs, int64_t refreshIntervalNs, uint32_t frameId);
    void                 Flush();
    void                 Finish();
    Backend::FenceHandle CreateFence();
    void                 DestroyFence(Backend::FenceHandle fh);
    Backend::IndexBufferHandle CreateIndexBuffer(Backend::ElementType type, uint32_t indexCount, Backend::BufferUsage usage);
    void                       DestroyIndexBuffer(Backend::IndexBufferHandle ibh);
    void                       ResetState();
    void                       Terminate();
    void                       QueueCommand(std::function<void()> command);

    // 绘制路径转发：命令先记入命令流，Flush() 后在渲染线程执行
    Backend::SwapChainHandle    CreateSwapChainHeadless(uint32_t width, uint32_t height, uint64_t flags);
    void                        DestroySwapChain(Backend::SwapChainHandle sch);
    Backend::ProgramHandle      CreateProgram(Backend::Program&& program);
    void                        DestroyProgram(Backend::ProgramHandle ph);
    Backend::VertexBufferInfoHandle CreateVertexBufferInfo(uint8_t bufferCount, uint8_t attributeCount, Backend::AttributeArray const& attributes);
    void                            DestroyVertexBufferInfo(Backend::VertexBufferInfoHandle vbih);
    Backend::VertexBufferHandle CreateVertexBuffer(uint32_t vertexCount, Backend::VertexBufferInfoHandle vbih);
    void                        DestroyVertexBuffer(Backend::VertexBufferHandle vbh);
    Backend::BufferObjectHandle CreateBufferObject(uint32_t byteCount, Backend::BufferObjectBinding bindingType, Backend::BufferUsage usage);
    void                        DestroyBufferObject(Backend::BufferObjectHandle boh);
    void                        UpdateBufferObject(Backend::BufferObjectHandle boh, Backend::BufferDescriptor&& data, uint32_t byteOffset);
    void                        SetVertexBufferObject(Backend::VertexBufferHandle vbh, uint32_t index, Backend::BufferObjectHandle boh);
    Backend::RenderPrimitiveHandle CreateRenderPrimitive(Backend::VertexBufferHandle vbh, Backend::IndexBufferHandle ibh, Backend::PrimitiveType pt);
    void                           DestroyRenderPrimitive(Backend::RenderPrimitiveHandle rph);
    Backend::RenderTargetHandle CreateDefaultRenderTarget();
    void                        MakeCurrent(Backend::SwapChainHandle drawSch, Backend::SwapChainHandle readSch);
    void                        BeginRenderPass(Backend::RenderTargetHandle rth, Backend::RenderPassParams const& params);
    void                        EndRenderPass();
    void                        BindPipeline(Backend::PipelineState const& state);
    void                        BindRenderPrimitive(Backend::RenderPrimitiveHandle rph);
    void                        DrawArrays(uint32_t vertexOffset, uint32_t vertexCount, uint32_t instanceCount);
    void                        Commit(Backend::SwapChainHandle sch);
    void                        ReadPixels(Backend::RenderTargetHandle src, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                                            Backend::PixelBufferDescriptor&& pbd);
    void                        WaitForReadPixels();

    // 把 finish() 投到渲染线程执行并等待其返回：读回请求的登记与投递都发生在该线程上
    bool FlushAndFinish();

    // 转发平台对象，供用例直接驱动交换链（非 Vulkan 平台返回 nullptr）
    NODISCARD Backend::VulkanPlatform* GetVulkanPlatform() const noexcept;

    // 转发驱动基类，供用例构造需要 DriverBase& 的组件（管线缓存）
    NODISCARD Backend::DriverBase* GetDriverBase() const noexcept;

private:
    Engine(Backend::BackendType type, Backend::DriverConfig config, size_t requiredSize, size_t bufferSize);
    ~Engine() override;
    bool init();
    void RunExecutionThread();

    Backend::PlatformPtr         m_platform;
    Backend::DriverPtr           m_driver;
    Backend::VulkanPlatform*     m_vkPlatform = nullptr;
    Backend::CommandBufferQueue  m_queue;
    Backend::CommandStreamPtr    m_stream;
    std::thread                  m_thread;
    bool                         m_terminated = false;
};

END_NS_TEST
