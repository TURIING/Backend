#pragma once

#include "Backend/DriverDefine.h"
#include "Utils/buffer/CircularBuffer.h"
#include "Utils/Compiler.h"
#include "Utils/mem/Ref.h"
#include "Utils/thread/lock/LockGuard.h"
#include "Utils/thread/lock/UniqueLock.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

BEGIN_NS_BACKEND

// 生产者-消费者命令队列：记录线程 Flush() 提交命令块，执行线程 WaitForCommands() 取走执行，
// 再经 ReleaseBuffer() 归还空间；空间不足时 Flush() 阻塞记录线程，实现背压控制
class CommandBufferQueue : public NS_UTILS::Ref {
public:
    // 待执行命令块的范围
    struct Range {
        void* begin;
        void* end;
    };

    // requiredSize 为 Flush() 后保证可用的空间，bufferSize 为环形缓冲大小（页对齐）
    CommandBufferQueue(size_t requiredSize, size_t bufferSize, bool paused);
    ~CommandBufferQueue() override;

    utils::CircularBuffer& GetCircularBuffer() noexcept { return m_circularBuffer; }
    utils::CircularBuffer const& GetCircularBuffer() const noexcept { return m_circularBuffer; }

    // 容量（requiredSize 向上取整到页后的值）
    size_t GetCapacity() const noexcept { return m_requiredSize; }

    // 历史最高占用水位（字节，仅 debug 构建下统计）
    size_t GetHighWatermark() const noexcept {
        utils::LockGuard const lock(m_lock);
        return m_highWatermark;
    }

    // 等待命令可用并返回全部待执行区间
    std::vector<Range> WaitForCommands() const;

    // 归还区间占用的空间，须按 WaitForCommands() 返回的顺序调用
    void ReleaseBuffer(Range const& buffer);

    // 提交已写入的命令块；空间不足时阻塞至 ReleaseBuffer() 归还
    void Flush();

    // 使 WaitForCommands() 立即返回
    void RequestExit();

    bool IsPaused() const noexcept;
    void SetPaused(bool paused);

    bool IsExitRequested() const;

private:
    size_t const m_requiredSize;

    utils::CircularBuffer m_circularBuffer;

    mutable std::mutex m_lock;
    mutable std::condition_variable m_condition;
    mutable std::vector<Range> m_commandBuffersToExecute UTILS_GUARDED_BY(m_lock);
    size_t m_freeSpace UTILS_GUARDED_BY(m_lock) = 0;
    size_t m_highWatermark UTILS_GUARDED_BY(m_lock) = 0;
    uint32_t m_exitRequested UTILS_GUARDED_BY(m_lock) = 0;
    bool m_paused UTILS_GUARDED_BY(m_lock) = false;

    // RequestExit() 写入的哨兵值
    static constexpr uint32_t kExitRequested = 0x31415926;
};

DECLARE_SHARE_PTR_CLASS(CommandBufferQueue);

END_NS_BACKEND
