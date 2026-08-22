#include "CommandBufferQueue.h"
#include "Command.h"

#include "Utils/Log.h"

#include <algorithm>
#include <new>
#include <utility>

BEGIN_NS_BACKEND

CommandBufferQueue::CommandBufferQueue(size_t const requiredSize, size_t const bufferSize, bool const paused)
        : m_requiredSize((requiredSize + (utils::CircularBuffer::GetBlockSize() - 1u)) &
                        ~(utils::CircularBuffer::GetBlockSize() - 1u)),
          m_circularBuffer(std::max(m_requiredSize, bufferSize)),
          m_freeSpace(m_circularBuffer.Size()),
          m_paused(paused) {
    LOG_ASSERT(m_circularBuffer.Size() >= m_requiredSize);
}

CommandBufferQueue::~CommandBufferQueue() {
    LOG_ASSERT(m_commandBuffersToExecute.empty());
}

void CommandBufferQueue::RequestExit() {
    utils::LockGuard const lock(m_lock);
    m_exitRequested = kExitRequested;
    m_condition.notify_one();
}

bool CommandBufferQueue::IsPaused() const noexcept {
    utils::LockGuard const lock(m_lock);
    return m_paused;
}

void CommandBufferQueue::SetPaused(bool const paused) {
    utils::LockGuard const lock(m_lock);
    if (paused) {
        m_paused = true;
    } else {
        m_paused = false;
        m_condition.notify_one();
    }
}

bool CommandBufferQueue::IsExitRequested() const {
    utils::LockGuard const lock(m_lock);
    return bool(m_exitRequested);
}

void CommandBufferQueue::Flush() {
    utils::CircularBuffer& circularBuffer = m_circularBuffer;
    if (circularBuffer.Empty()) {
        return;
    }

    // 追加终止命令：nullptr 使命令链在执行末端归零
    new (circularBuffer.Allocate(sizeof(NoopCommand))) NoopCommand(nullptr);

    size_t const requiredSize = m_requiredSize;

    auto const range = circularBuffer.GetBuffer();
    LOG_ASSERT(circularBuffer.Empty());

    size_t const used = static_cast<size_t>(
            static_cast<char const*>(range.head) - static_cast<char const*>(range.tail));

    utils::UniqueLock lock(m_lock);

    // 缓冲配置过小，命令流已损坏且不可恢复
    if (used > m_freeSpace) {
        LOG_CRITICAL("CommandStream overflow: commands are corrupted and unrecoverable. "
                     "used={} bytes, overflow={} bytes. "
                     "Please increase requiredSize / bufferSize.",
                used, used - m_freeSpace);
    }

    m_freeSpace -= used;
    m_commandBuffersToExecute.push_back({ range.tail, range.head });
    m_condition.notify_one();

    // 等待直到缓冲区有足够空间（背压）
    if (m_freeSpace < requiredSize) {
#ifndef NDEBUG
        size_t const totalUsed = circularBuffer.Size() - m_freeSpace;
        LOG_DEBUG("CommandStream used too much space (will block): "
                  "needed={} out of {}, totalUsed={}, current={}, queue size={} buffers",
                requiredSize, m_freeSpace, totalUsed, used, m_commandBuffersToExecute.size());

        m_highWatermark = std::max(m_highWatermark, totalUsed);
#endif

        if (m_paused) {
            LOG_CRITICAL("CommandStream is full, but since the rendering thread is paused, "
                         "the buffer cannot flush and we will deadlock. Instead, abort.");
        }

        while (m_freeSpace < requiredSize) {
            m_condition.wait(lock);
        }
    }
}

std::vector<CommandBufferQueue::Range> CommandBufferQueue::WaitForCommands() const {
    utils::UniqueLock lock(m_lock);
    while ((m_commandBuffersToExecute.empty() || m_paused) && !m_exitRequested) {
        m_condition.wait(lock);
    }
    return std::move(m_commandBuffersToExecute);
}

void CommandBufferQueue::ReleaseBuffer(CommandBufferQueue::Range const& buffer) {
    size_t const used = static_cast<size_t>(
            static_cast<char const*>(buffer.end) - static_cast<char const*>(buffer.begin));
    utils::LockGuard const lock(m_lock);
    m_freeSpace += used;
    m_condition.notify_one();
}

END_NS_BACKEND
