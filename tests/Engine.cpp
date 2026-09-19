#include "Engine.h"

#include "Backend/Driver.h"
#include "Backend/Namespace.h"

#include <condition_variable>
#include <mutex>
#include <utility>
#include <vector>

#include "DriverBase.h"
#include "vulkan/VulkanDriver.h"

BEGIN_NS_TEST

constexpr size_t kDefaultRequiredSize = 1 << 20;
constexpr size_t kDefaultBufferSize   = 2 << 20;

struct Engine::BuilderDetails {
    Backend::BackendType  type = Backend::BackendType::VULKAN;
    Backend::DriverConfig config;
    size_t                requiredSize = kDefaultRequiredSize;
    size_t                bufferSize   = kDefaultBufferSize;
};

Engine::Builder::Builder() noexcept = default;

Engine::Builder::~Builder() noexcept = default;

Engine::Builder& Engine::Builder::BackendType(Backend::BackendType type) {
    m_pImpl->type = type;
    return *this;
}

Engine::Builder& Engine::Builder::DriverConfig(Backend::DriverConfig const& config) {
    m_pImpl->config = config;
    return *this;
}

Engine::Builder& Engine::Builder::BufferSize(size_t requiredSize, size_t bufferSize) {
    m_pImpl->requiredSize = requiredSize;
    m_pImpl->bufferSize   = bufferSize;
    return *this;
}

EnginePtr Engine::Builder::Build() {
    EnginePtr engine(new Engine(m_pImpl->type, m_pImpl->config, m_pImpl->requiredSize, m_pImpl->bufferSize));
    if (!engine->init()) {
        return {};
    }
    return engine;
}

Engine::Engine(Backend::BackendType type, Backend::DriverConfig config, size_t requiredSize, size_t bufferSize)
    : m_platform(Backend::PlatformFactory::Create(type)),
      m_driver(m_platform ? m_platform->CreateDriver(config, nullptr) : nullptr),
      m_queue(NS_UTILS::MakeShared<Backend::CommandBufferQueue>(requiredSize, bufferSize, false)) {}

Engine::~Engine() { Terminate(); }

bool Engine::init() {
    if (!m_driver) {
        LOG_CRITICAL("Vulkan driver initialization failed");
        return false;
    }

    m_stream = new Backend::CommandStream(m_driver, m_queue->GetCircularBuffer());
    m_stream->DebugThreading();

    m_thread = std::thread([this] { RunExecutionThread(); });
    return true;
}

void Engine::RunExecutionThread() {
    while (true) {
        std::vector<Backend::CommandBufferQueue::Range> const ranges = m_queue->WaitForCommands();
        if (ranges.empty()) {
            if (m_queue->IsExitRequested()) {
                break;
            }
            continue;
        }
        for (Backend::CommandBufferQueue::Range const& range : ranges) {
            m_stream->Execute(range.begin);
            m_queue->ReleaseBuffer(range);
        }
    }
}

void Engine::BeginFrame(int64_t monotonicClockNs, int64_t refreshIntervalNs, uint32_t frameId) {
    m_stream->beginFrame(monotonicClockNs, refreshIntervalNs, frameId);
}

void Engine::Flush() { m_queue->Flush(); }

void Engine::Finish() { FlushAndFinish(); }

Backend::FenceHandle Engine::CreateFence() { return m_stream->createFence(); }

void Engine::DestroyFence(Backend::FenceHandle fh) { m_stream->destroyFence(fh); }

Backend::IndexBufferHandle Engine::CreateIndexBuffer(Backend::ElementType type, uint32_t indexCount, Backend::BufferUsage usage) {
    return m_stream->CreateIndexBuffer(type, indexCount, usage);
}

void Engine::DestroyIndexBuffer(Backend::IndexBufferHandle ibh) { m_stream->DestroyIndexBuffer(ibh); }

void Engine::ResetState() { m_stream->resetState(); }

void Engine::QueueCommand(std::function<void()> command) { m_stream->QueueCommand(std::move(command)); }

Backend::PlatformPtr Engine::GetPlatform() const noexcept { return m_platform; }

Backend::SwapChainHandle Engine::CreateSwapChainHeadless(uint32_t width, uint32_t height, uint64_t flags) {
    return m_stream->CreateSwapChainHeadless(width, height, flags);
}

Backend::SwapChainHandle Engine::CreateSwapChain(void* nativeWindow, uint64_t flags) { return m_stream->CreateSwapChain(nativeWindow, flags); }

void Engine::DestroySwapChain(Backend::SwapChainHandle sch) { m_stream->DestroySwapChain(sch); }

Backend::ProgramHandle Engine::CreateProgram(Backend::Program&& program) { return m_stream->CreateProgram(std::move(program)); }

void Engine::DestroyProgram(Backend::ProgramHandle ph) { m_stream->DestroyProgram(ph); }

Backend::VertexBufferInfoHandle Engine::CreateVertexBufferInfo(uint8_t bufferCount, uint8_t attributeCount,
                                                               Backend::AttributeArray const& attributes) {
    return m_stream->CreateVertexBufferInfo(bufferCount, attributeCount, attributes);
}

void Engine::DestroyVertexBufferInfo(Backend::VertexBufferInfoHandle vbih) { m_stream->DestroyVertexBufferInfo(vbih); }

Backend::VertexBufferHandle Engine::CreateVertexBuffer(uint32_t vertexCount, Backend::VertexBufferInfoHandle vbih) {
    return m_stream->CreateVertexBuffer(vertexCount, vbih);
}

void Engine::DestroyVertexBuffer(Backend::VertexBufferHandle vbh) { m_stream->DestroyVertexBuffer(vbh); }

Backend::BufferObjectHandle Engine::CreateBufferObject(uint32_t byteCount, Backend::BufferObjectBinding bindingType, Backend::BufferUsage usage) {
    return m_stream->CreateBufferObject(byteCount, bindingType, usage);
}

void Engine::DestroyBufferObject(Backend::BufferObjectHandle boh) { m_stream->DestroyBufferObject(boh); }

void Engine::UpdateBufferObject(Backend::BufferObjectHandle boh, Backend::BufferDescriptor&& data, uint32_t byteOffset) {
    m_stream->UpdateBufferObject(boh, std::move(data), byteOffset);
}

void Engine::SetVertexBufferObject(Backend::VertexBufferHandle vbh, uint32_t index, Backend::BufferObjectHandle boh) {
    m_stream->SetVertexBufferObject(vbh, index, boh);
}

Backend::RenderPrimitiveHandle Engine::CreateRenderPrimitive(Backend::VertexBufferHandle vbh, Backend::IndexBufferHandle ibh,
                                                             Backend::PrimitiveType pt) {
    return m_stream->CreateRenderPrimitive(vbh, ibh, pt);
}

void Engine::DestroyRenderPrimitive(Backend::RenderPrimitiveHandle rph) { m_stream->DestroyRenderPrimitive(rph); }

Backend::RenderTargetHandle Engine::CreateDefaultRenderTarget() { return m_stream->CreateDefaultRenderTarget(); }

void Engine::MakeCurrent(Backend::SwapChainHandle drawSch, Backend::SwapChainHandle readSch) { m_stream->MakeCurrent(drawSch, readSch); }

void Engine::BeginRenderPass(Backend::RenderTargetHandle rth, Backend::RenderPassParams const& params) { m_stream->BeginRenderPass(rth, params); }

void Engine::EndRenderPass() { m_stream->EndRenderPass(); }

void Engine::BindPipeline(Backend::PipelineState const& state) { m_stream->BindPipeline(state); }

void Engine::BindRenderPrimitive(Backend::RenderPrimitiveHandle rph) { m_stream->BindRenderPrimitive(rph); }

void Engine::DrawArrays(uint32_t vertexOffset, uint32_t vertexCount, uint32_t instanceCount) {
    m_stream->DrawArrays(vertexOffset, vertexCount, instanceCount);
}

void Engine::Commit(Backend::SwapChainHandle sch) { m_stream->Commit(sch); }

void Engine::ReadPixels(Backend::RenderTargetHandle src, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                        Backend::PixelBufferDescriptor&& pbd) {
    m_stream->ReadPixels(src, x, y, width, height, std::move(pbd));

    // ReadPixels 的记录与读回线程的投递都发生在渲染线程上：若在调用线程直接调 finish()，
    // readPixels 可能尚未登记，Drain 也就等不到结果。故把 finish 本身也交给渲染线程执行
    FlushAndFinish();
}

bool Engine::FlushAndFinish() {
    std::mutex              mutex;
    std::condition_variable condition;
    bool                    finished = false;

    m_stream->QueueCommand([&] {
        // 经命令流再调 finish() 会从命令流分配命令，而执行本命令的渲染线程不是录制线程；
        // 直接调驱动即可。此处用具体类型指针：DriverBase 侧的同名非虚包装不会转发到实现
        static_cast<Backend::VulkanDriver*>(m_driver.Get())->finish(0);
        {
            std::lock_guard const lock(mutex);
            finished = true;
        }
        condition.notify_one();
    });
    m_queue->Flush();

    std::unique_lock lock(mutex);
    condition.wait(lock, [&finished] { return finished; });
    return true;
}

void Engine::WaitForReadPixels() {
    // 读回请求登记在渲染线程上，故必须先把线程推过去
    FlushAndFinish();
}

Backend::DriverBase* Engine::GetDriverBase() const noexcept { return static_cast<Backend::DriverBase*>(m_driver.Get()); }

void Engine::Terminate() {
    if (m_terminated) {
        return;
    }
    m_queue->Flush();
    m_queue->RequestExit();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_driver->terminate();
    m_terminated = true;
}

END_NS_TEST
