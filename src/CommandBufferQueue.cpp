/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "CommandBufferQueue.h"
#include "CommandStream.h"

#include <algorithm>
#include <exception>
#include <mutex>
#include <utility>

BEGIN_NS_BACKEND

CommandBufferQueue::CommandBufferQueue(size_t const requiredSize, size_t const bufferSize, bool const paused)
        : m_requiredSize((requiredSize + (CircularBuffer::getBlockSize() - 1u)) &
                        ~(CircularBuffer::getBlockSize() - 1u)),
          m_circularBuffer(std::max(m_requiredSize, bufferSize)),
          m_freeSpace(m_circularBuffer.size()),
          m_paused(paused) {
    LOG_ASSERT(m_circularBuffer.size() >= m_requiredSize);
}

CommandBufferQueue::~CommandBufferQueue() {
    LOG_ASSERT(m_commandBuffersToExecute.empty());
}

void CommandBufferQueue::requestExit() {
    std::lock_guard const lock(m_lock);
    m_exitRequested = kExitRequested;
    m_condition.notify_one();
}

bool CommandBufferQueue::isPaused() const noexcept {
    std::lock_guard const lock(m_lock);
    return m_paused;
}

void CommandBufferQueue::setPaused(bool const paused) {
    std::lock_guard const lock(m_lock);
    if (paused) {
        m_paused = true;
    } else {
        m_paused = false;
        m_condition.notify_one();
    }
}

bool CommandBufferQueue::isExitRequested() const {
    std::lock_guard const lock(m_lock);
    return bool(m_exitRequested);
}

#ifdef __EXCEPTIONS
void CommandBufferQueue::propagateBackendException() const {
    if (hasUnrecoverableError()) {
        if (!m_exceptionRethrown.exchange(true, std::memory_order_relaxed)) {
            std::rethrow_exception(m_backendException);
        } else {
            LOG_CRITICAL("Engine is in unrecoverable state due to previous backend exception");
        }
    }
}
#endif

void CommandBufferQueue::flush() {
#ifdef __EXCEPTIONS
    if (hasUnrecoverableError()) {
        // 丢弃当前命令块，避免环形缓冲区被填满
        m_circularBuffer.getBuffer();
        propagateBackendException();
    }
#endif

    CircularBuffer& circularBuffer = m_circularBuffer;
    if (circularBuffer.empty()) {
        return;
    }

    // 追加终止命令：nullptr 使命令链在执行末端归零
    new(circularBuffer.allocate(sizeof(NoopCommand))) NoopCommand(nullptr);

    const size_t requiredSize = m_requiredSize;

    // 取出当前命令块
    auto const [begin, end] = circularBuffer.getBuffer();
    LOG_ASSERT(circularBuffer.empty());

    // 当前命令块大小
    size_t const used = static_cast<size_t>(
            static_cast<char const*>(end) - static_cast<char const*>(begin));

    std::unique_lock lock(m_lock);

    // 环形缓冲区过小，命令流已损坏且不可恢复
    if (used > m_freeSpace) {
        LOG_CRITICAL("CommandStream overflow: commands are corrupted and unrecoverable. "
                     "used={} bytes, overflow={} bytes. "
                     "Please increase requiredSize / bufferSize.",
                used, used - m_freeSpace);
    }

    m_freeSpace -= used;
    m_commandBuffersToExecute.push_back({ begin, end });
    m_condition.notify_one();

    // 等待直到缓冲区有足够空间
    if (m_freeSpace < requiredSize) {
#ifndef NDEBUG
        size_t const totalUsed = circularBuffer.size() - m_freeSpace;
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

std::vector<CommandBufferQueue::Range> CommandBufferQueue::waitForCommands() const {
    std::unique_lock lock(m_lock);
    while ((m_commandBuffersToExecute.empty() || m_paused) && !m_exitRequested) {
        m_condition.wait(lock);
    }
    return std::move(m_commandBuffersToExecute);
}

void CommandBufferQueue::releaseBuffer(CommandBufferQueue::Range const& buffer) {
    size_t const used = static_cast<size_t>(
            static_cast<char const*>(buffer.end) - static_cast<char const*>(buffer.begin));
    std::lock_guard const lock(m_lock);
    m_freeSpace += used;
    m_condition.notify_one();
}

END_NS_BACKEND
