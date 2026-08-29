#include "Engine.h"

#include <utility>
#include <vector>

BEGIN_NS_TEST

constexpr size_t kDefaultRequiredSize = 1 << 20;
constexpr size_t kDefaultBufferSize   = 2 << 20;

struct Engine::BuilderDetails {
    Backend::BackendType  type         = Backend::BackendType::VULKAN;
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
    EnginePtr engine(
            new Engine(m_pImpl->type, m_pImpl->config, m_pImpl->requiredSize, m_pImpl->bufferSize));
    if (!engine->init()) {
        return {};
    }
    return engine;
}

Engine::Engine(Backend::BackendType type, Backend::DriverConfig config, size_t requiredSize, size_t bufferSize)
        : m_platform(Backend::PlatformFactory::Create(type)),
          m_driver(m_platform ? m_platform->CreateDriver(config, nullptr) : Backend::DriverPtr()),
          m_queue(requiredSize, bufferSize, false) {
}

Engine::~Engine() {
    Terminate();
}

bool Engine::init() {
    // 平台或驱动创建失败说明 Vulkan 初始化链路已断，继续执行只会空转
    if (!m_driver) {
        LOG_CRITICAL("Vulkan driver initialization failed");
        return false;
    }

    m_stream = Backend::CommandStreamPtr(new Backend::CommandStream(m_driver, m_queue.GetCircularBuffer()));
    m_stream->DebugThreading();

    m_thread = std::thread([this] { RunExecutionThread(); });
    return true;
}

void Engine::RunExecutionThread() {
    while (true) {
        std::vector<Backend::CommandBufferQueue::Range> const ranges = m_queue.WaitForCommands();
        if (ranges.empty()) {
            if (m_queue.IsExitRequested()) {
                break;
            }
            continue;
        }
        for (Backend::CommandBufferQueue::Range const& range : ranges) {
            m_stream->Execute(range.begin);
            m_queue.ReleaseBuffer(range);
        }
    }
}

void Engine::BeginFrame(int64_t monotonicClockNs, int64_t refreshIntervalNs, uint32_t frameId) {
    m_stream->beginFrame(monotonicClockNs, refreshIntervalNs, frameId);
}

void Engine::Flush() {
    m_queue.Flush();
}

void Engine::Finish() {
    m_stream->finish();
}

Backend::FenceHandle Engine::CreateFence() {
    return m_stream->createFence();
}

void Engine::DestroyFence(Backend::FenceHandle fh) {
    m_stream->destroyFence(fh);
}

void Engine::ResetState() {
    m_stream->resetState();
}

void Engine::QueueCommand(std::function<void()> command) {
    m_stream->QueueCommand(std::move(command));
}

void Engine::Terminate() {
    if (m_terminated) {
        return;
    }
    m_queue.Flush();
    m_queue.RequestExit();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_driver->terminate();
    m_terminated = true;
}

END_NS_TEST
