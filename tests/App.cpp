#include "App.h"

#include "Utils/Log.h"

BEGIN_NS_TEST

namespace {
constexpr uint32_t kFrameCount        = 8;
constexpr int64_t  kRefreshIntervalNs = 16666;
constexpr uint32_t kIndexCount        = 1024;
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

        // 交替 16 位与 32 位索引，覆盖元素宽度换算的两条分支
        Backend::ElementType const    elementType = (frame % 2 == 0) ? Backend::ElementType::USHORT : Backend::ElementType::UINT;
        Backend::IndexBufferHandle const ibh      = engine->CreateIndexBuffer(elementType, kIndexCount, Backend::BufferUsage::STATIC);
        LOG_INFO("frame {}: index buffer id={}, element size={}", frame, ibh.GetId(), Backend::Driver::GetElementTypeSize(elementType));
        engine->DestroyIndexBuffer(ibh);

        engine->Flush();
    }

    cleanupCallback(engine);
    engine->Terminate();
}

END_NS_TEST
