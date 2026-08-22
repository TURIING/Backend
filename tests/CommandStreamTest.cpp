#include "command/CommandBufferQueue.h"
#include "command/CommandStream.h"
#include "command/CommandStreamDispatcher.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

BEGIN_NS_BACKEND

constexpr uint32_t kFenceId = 42;

// 测试双驱动：记录各方法调用次数与顺序
class NoopDriver : public Driver {
public:
    struct Counts {
        int tick = 0;
        int beginFrame = 0;
        int flush = 0;
        int finish = 0;
        int resetState = 0;
        int createFenceS = 0;
        int createFenceR = 0;
        int destroyFence = 0;
        int terminate = 0;
        FenceHandle lastFence;
    };

    Dispatcher GetDispatcher() const noexcept override {
        return ConcreteDispatcher<NoopDriver>::Make();
    }

    Counts const& GetCounts() const noexcept { return m_counts; }
    std::vector<std::string> const& GetEvents() const noexcept { return m_events; }

    void terminate() override {
        ++m_counts.terminate;
        m_events.push_back("terminate");
    }

    void tick(int) {
        ++m_counts.tick;
        m_events.push_back("tick");
    }

    void beginFrame(int64_t, int64_t, uint32_t) {
        ++m_counts.beginFrame;
        m_events.push_back("beginFrame");
    }

    void flush(int) {
        ++m_counts.flush;
        m_events.push_back("flush");
    }

    void finish(int) {
        ++m_counts.finish;
        m_events.push_back("finish");
    }

    void resetState(int) {
        ++m_counts.resetState;
        m_events.push_back("resetState");
    }

    FenceHandle createFenceS() noexcept override {
        ++m_counts.createFenceS;
        m_events.push_back("createFenceS");
        return FenceHandle(kFenceId);
    }

    void createFenceR(FenceHandle fh, utils::ImmutableString&&) {
        ++m_counts.createFenceR;
        m_counts.lastFence = fh;
        m_events.push_back("createFenceR");
    }

    void destroyFence(FenceHandle) {
        ++m_counts.destroyFence;
        m_events.push_back("destroyFence");
    }

private:
    Counts m_counts;
    std::vector<std::string> m_events;
};

namespace {

// bufferSize 需为单帧所需空间的 2 倍以上，否则 Flush 会因背压阻塞
constexpr size_t kRequiredSize = 1 << 20;
constexpr size_t kBufferSize = 2 << 20;

struct Fixture {
    NoopDriver* nd = new NoopDriver();
    DriverPtr driver{nd};
    CommandBufferQueue queue{kRequiredSize, kBufferSize, false};
    CommandStream stream{driver, queue.GetCircularBuffer()};
};

void ExecuteAndRelease(Fixture& f) {
    f.queue.Flush();
    std::vector<CommandBufferQueue::Range> const ranges = f.queue.WaitForCommands();
    for (CommandBufferQueue::Range const& r : ranges) {
        f.stream.Execute(r.begin);
        f.queue.ReleaseBuffer(r);
    }
}

} // namespace

TEST(CommandStreamTest, ExecuteCommandChain) {
    Fixture f;

    f.stream.beginFrame(1000, 16666, 1);
    f.stream.flush();
    f.stream.finish();
    ExecuteAndRelease(f);

    NoopDriver::Counts const& counts = f.nd->GetCounts();
    EXPECT_EQ(counts.beginFrame, 1);
    EXPECT_EQ(counts.flush, 1);
    EXPECT_EQ(counts.finish, 1);
    EXPECT_EQ(f.nd->GetEvents(), (std::vector<std::string>{"beginFrame", "flush", "finish"}));
}

TEST(CommandStreamTest, ReturnPath) {
    Fixture f;

    FenceHandle h = f.stream.createFence();
    EXPECT_TRUE(h);
    EXPECT_EQ(h.GetId(), kFenceId);
    // 返回值在入队阶段同步取得，R 命令尚未执行
    EXPECT_EQ(f.nd->GetCounts().createFenceS, 1);
    EXPECT_EQ(f.nd->GetCounts().createFenceR, 0);

    ExecuteAndRelease(f);

    EXPECT_EQ(f.nd->GetCounts().createFenceR, 1);
    EXPECT_EQ(f.nd->GetCounts().lastFence, h);
    EXPECT_EQ(f.nd->GetEvents(), (std::vector<std::string>{"createFenceS", "createFenceR"}));
}

TEST(CommandStreamTest, SynchronousCall) {
    Fixture f;

    f.stream.terminate();

    // 同步方法立即直调驱动，不产生命令
    EXPECT_EQ(f.nd->GetCounts().terminate, 1);
    EXPECT_EQ(f.nd->GetEvents(), (std::vector<std::string>{"terminate"}));
}

TEST(CommandStreamTest, AllocateSkipsAuxMemory) {
    Fixture f;

    f.stream.beginFrame(1000, 16666, 1);
    void* mem = f.stream.Allocate(64, 16);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(mem) % 16, 0);
    f.stream.flush();
    ExecuteAndRelease(f);

    EXPECT_EQ(f.nd->GetCounts().beginFrame, 1);
    EXPECT_EQ(f.nd->GetCounts().flush, 1);
}

TEST(CommandStreamTest, QueueCommandRunsLambda) {
    Fixture f;

    bool ran = false;
    f.stream.QueueCommand([&ran] { ran = true; });
    ExecuteAndRelease(f);

    EXPECT_TRUE(ran);
}

TEST(CommandStreamTest, MultiFrameLoop) {
    Fixture f;

    for (uint32_t frame = 0; frame < 3; ++frame) {
        f.stream.beginFrame(1000, 16666, frame);
        FenceHandle h = f.stream.createFence();
        f.stream.destroyFence(h);
        f.stream.flush();
        ExecuteAndRelease(f);
    }

    NoopDriver::Counts const& counts = f.nd->GetCounts();
    EXPECT_EQ(counts.beginFrame, 3);
    EXPECT_EQ(counts.createFenceS, 3);
    EXPECT_EQ(counts.createFenceR, 3);
    EXPECT_EQ(counts.destroyFence, 3);
    EXPECT_EQ(counts.flush, 3);
    EXPECT_EQ(counts.tick, 0);
}

END_NS_BACKEND
