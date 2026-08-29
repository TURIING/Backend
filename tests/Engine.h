#pragma once

#include "Backend/DriverDefine.h"
#include "Backend/PlatformFactory.h"

#include "Utils/Utils.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>

#include "Macro.h"
#include "command/CommandBufferQueue.h"
#include "command/CommandStream.h"

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
    void                 ResetState();
    void                 Terminate();
    void                 QueueCommand(std::function<void()> command);

private:
    Engine(Backend::BackendType type, Backend::DriverConfig config, size_t requiredSize, size_t bufferSize);
    ~Engine() override;
    bool init();
    void RunExecutionThread();

    Backend::PlatformPtr        m_platform;
    Backend::DriverPtr          m_driver;
    Backend::CommandBufferQueue m_queue;
    Backend::CommandStreamPtr   m_stream;
    std::thread                 m_thread;
    bool                        m_terminated = false;
};

END_NS_TEST
