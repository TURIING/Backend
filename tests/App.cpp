#include "App.h"

#include "Utils/Log.h"

BEGIN_NS_TEST

namespace {
constexpr uint32_t kFrameCount        = 8;
constexpr int64_t  kRefreshIntervalNs = 16666;
}  // namespace

App& App::Instance() {
    static App sInstance;
    return sInstance;
}

void App::Run(const SetupCallback& setupCallback, const CleanUpCallback& cleanupCallback) {
    EnginePtr engine = Engine::Builder().BackendType(Backend::BackendType::VULKAN).Build();
    if (!engine) {
        return;
    }

    setupCallback(engine);

    // 缓冲背压（Flush 空间不足时阻塞）自动限制记录速度，无需手动限速
    for (uint32_t frame = 0; frame < kFrameCount; ++frame) {
        engine->BeginFrame(0, kRefreshIntervalNs, frame);
        Backend::FenceHandle const fence = engine->CreateFence();
        LOG_INFO("frame {}: fence id={}", frame, fence.GetId());
        engine->DestroyFence(fence);
        engine->Flush();
    }

    cleanupCallback(engine);
    engine->Terminate();
}

END_NS_TEST
