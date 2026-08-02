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

/**
 * @brief 生产者-消费者命令队列
 *
 * 以 CircularBuffer 为主要存储：记录线程调用 flush() 提交当前命令块，
 * 执行线程调用 waitForCommands() 取走命令块执行，再通过 releaseBuffer() 归还空间。
 * 空间不足时 flush() 会阻塞记录线程，从而天然实现背压控制。
 *
 * 继承 Ref 以对齐项目对象模型，跨线程共享时可用 CommandBufferQueuePtr 持有。
 */
class CommandBufferQueue : public NS_UTILS::Ref {
public:
    /** @brief 待执行命令块的范围 */
    struct Range {
        void* begin;
        void* end;
    };

    /**
     * @brief 构造命令队列
     * @param requiredSize flush() 后保证可用的空间（向上取整到页大小）
     * @param bufferSize   环形缓冲区大小，须为页大小整数倍，不足时向上取整
     * @param paused       初始是否挂起（挂起时 waitForCommands() 不返回）
     */
    CommandBufferQueue(size_t requiredSize, size_t bufferSize, bool paused);
    ~CommandBufferQueue() override;

    /** @brief 获取底层环形缓冲区 */
    CircularBuffer&       getCircularBuffer() noexcept { return m_circularBuffer; }
    CircularBuffer const& getCircularBuffer() const noexcept { return m_circularBuffer; }

    /** @brief 容量（requiredSize 向上取整到页后的值） */
    size_t getCapacity() const noexcept { return m_requiredSize; }

    /** @brief 历史最高占用水位（字节，仅在 debug 构建下统计） */
    size_t getHighWatermark() const noexcept {
        std::lock_guard const lock(m_lock);
        return m_highWatermark;
    }

    /**
     * @brief 阻塞等待命令可用，返回待执行命令块数组
     * @return 待执行命令块；调用 requestExit() 后会立即返回
     */
    std::vector<Range> waitForCommands() const;

    /**
     * @brief 归还命令块内存到环形缓冲区
     * @note 必须按 waitForCommands() 返回的顺序调用
     */
    void releaseBuffer(Range const& buffer);

    /**
     * @brief 提交当前命令块
     * @note 阻塞直到环形缓冲区至少 m_requiredSize 字节可用
     */
    void flush();

    /** @brief 让 waitForCommands() 立即返回 */
    void requestExit();

    /** @brief 是否挂起 */
    bool isPaused() const noexcept;
    /** @brief 挂起/恢复队列 */
    void setPaused(bool paused);

    /** @brief 是否已请求退出 */
    bool isExitRequested() const;

#ifdef __EXCEPTIONS
    /** @brief 是否发生了不可恢复的后端异常 */
    bool hasUnrecoverableError() const noexcept { return m_hasUnrecoverableError.load(std::memory_order_acquire); }
    /** @brief 记录不可恢复异常，后续 flush()/waitForCommands() 将重新抛出 */
    void setUnrecoverableException(std::exception_ptr e) noexcept {
        m_backendException = e;
        m_hasUnrecoverableError.store(true, std::memory_order_release);
    }
    /** @brief 重新抛出已记录的后端异常 */
    void propagateBackendException() const;
    /** @brief 该异常是否已被重新抛出过 */
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
