#pragma once

#include "Backend/DriverDefine.h"
#include "CircularBuffer.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <vector>

BEGIN_NS_BACKEND

// 生产者-消费者命令队列：记录线程 flush() 提交命令块，执行线程 waitForCommands() 取走执行，
// 再经 releaseBuffer() 归还空间；空间不足时 flush() 阻塞记录线程，实现背压控制。
class CommandBufferQueue : public NS_UTILS::Ref {
public:
    // 待执行命令块的范围
    struct Range {
        void* begin;
        void* end;
    };

    // paused 挂起时 waitForCommands() 不返回
    CommandBufferQueue(size_t requiredSize, size_t bufferSize, bool paused);
    ~CommandBufferQueue() override;

    CircularBuffer&       getCircularBuffer() noexcept { return m_circularBuffer; }
    CircularBuffer const& getCircularBuffer() const noexcept { return m_circularBuffer; }

    // 容量（requiredSize 向上取整到页后的值）
    size_t getCapacity() const noexcept { return m_requiredSize; }

    // 历史最高占用水位（字节，仅 debug 构建下统计）
    size_t getHighWatermark() const noexcept {
        std::lock_guard const lock(m_lock);
        return m_highWatermark;
    }

    std::vector<Range> waitForCommands() const;

    // 必须按 waitForCommands() 返回的顺序调用
    void releaseBuffer(Range const& buffer);

    void flush();

    void requestExit();

    bool isPaused() const noexcept;
    void setPaused(bool paused);

    bool isExitRequested() const;

#ifdef __EXCEPTIONS
    bool hasUnrecoverableError() const noexcept { return m_hasUnrecoverableError.load(std::memory_order_acquire); }
    void setUnrecoverableException(std::exception_ptr e) noexcept {
        m_backendException = e;
        m_hasUnrecoverableError.store(true, std::memory_order_release);
    }
    void           propagateBackendException() const;
    bool hasExceptionBeenRethrown() const noexcept { return m_exceptionRethrown.load(std::memory_order_relaxed); }
#else
    void           propagateBackendException() const noexcept {}
    constexpr bool hasUnrecoverableError() const noexcept { return false; }
    constexpr bool hasExceptionBeenRethrown() const noexcept { return false; }
#endif

private:
    const size_t m_requiredSize;

    CircularBuffer m_circularBuffer;

    mutable std::mutex              m_lock;
    mutable std::condition_variable m_condition;
    mutable std::vector<Range>      m_commandBuffersToExecute;
    size_t                          m_freeSpace     = 0;
    size_t                          m_highWatermark = 0;
    uint32_t                        m_exitRequested = 0;
    bool                            m_paused        = false;

    // requestExit() 写入的哨兵值
    static constexpr uint32_t kExitRequested = 0x31415926;

#ifdef __EXCEPTIONS
    mutable std::exception_ptr m_backendException;
    std::atomic<bool>          m_hasUnrecoverableError{ false };
    mutable std::atomic<bool>  m_exceptionRethrown{ false };
#endif
};

DECLARE_SHARE_PTR_CLASS(CommandBufferQueue);

END_NS_BACKEND
