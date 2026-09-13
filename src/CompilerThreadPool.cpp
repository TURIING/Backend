#include "CompilerThreadPool.h"

#include "Utils/Log.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <utility>

BEGIN_NS_BACKEND

ProgramToken::~ProgramToken() = default;

CompilerThreadPool::CompilerThreadPool() noexcept = default;

CompilerThreadPool::~CompilerThreadPool() noexcept {
    LOG_ASSERT(m_compilerThreads.empty());
    LOG_ASSERT(m_queues[0].empty());
    LOG_ASSERT(m_queues[1].empty());
    LOG_ASSERT(m_queues[2].empty());
}

void CompilerThreadPool::Init(uint32_t const threadCount, ThreadSetup &&threadSetup, ThreadCleanup &&threadCleanup) {
    auto setup   = std::make_shared<ThreadSetup>(std::move(threadSetup));
    auto cleanup = std::make_shared<ThreadCleanup>(std::move(threadCleanup));

    for (uint32_t i = 0; i < threadCount; ++i) {
        m_compilerThreads.emplace_back([this, setup, cleanup]() {
            (*setup)();

            for (;;) {
                std::unique_lock lock(m_queueLock);
                m_queueCondition.wait(lock, [this]() {
                    return m_exitRequested || !std::all_of(m_queues.begin(), m_queues.end(), [](auto const &queue) { return queue.empty(); });
                });

                if (m_exitRequested) {
                    break;
                }

                Job job;
                // 取第一条非空队列：下标顺序即优先级顺序
                auto &queue = [this]() -> TaskQueue & {
                    for (auto &candidate : m_queues) {
                        if (!candidate.empty()) {
                            return candidate;
                        }
                    }
                    return m_queues[0];
                }();
                LOG_ASSERT(!queue.empty());
                std::swap(job, queue.front().second);
                queue.pop_front();

                // 任务体在锁外执行：编译耗时长，持锁会阻塞其他线程取任务
                lock.unlock();
                job();
            }

            (*cleanup)();
        });
    }
}

auto CompilerThreadPool::Find(ProgramTokenPtr const &token) -> std::pair<TaskQueue &, TaskQueue::iterator> {
    for (auto &queue : m_queues) {
        auto pos = std::find_if(queue.begin(), queue.end(), [&token](auto const &item) { return item.first == token; });
        if (pos != queue.end()) {
            return { queue, pos };
        }
    }
    // token 对应的任务可能正在执行中
    return { m_queues[0], m_queues[0].end() };
}

auto CompilerThreadPool::Dequeue(ProgramTokenPtr const &token) -> Job {
    std::lock_guard const lock(m_queueLock);

    Job  job;
    auto result = Find(token);
    if (result.second != result.first.end()) {
        std::swap(job, result.second->second);
        result.first.erase(result.second);
    }
    return job;
}

void CompilerThreadPool::Queue(CompilerPriorityQueue priorityQueue, ProgramTokenPtr const &token, Job &&job) {
    std::lock_guard const lock(m_queueLock);
    m_queues[static_cast<size_t>(priorityQueue)].emplace_back(token, std::move(job));
    m_queueCondition.notify_one();
}

void CompilerThreadPool::Terminate() noexcept {
    {
        std::lock_guard const lock(m_queueLock);
        m_exitRequested = true;
        m_queueCondition.notify_all();
    }

    // 必须在 join 前释放队列锁：工作线程从条件变量醒来后要重新取 m_queueLock，
    // 若此处持锁阻塞在 join 上会死锁
    for (auto &thread : m_compilerThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    m_compilerThreads.clear();

    // 丢弃剩余任务：此时工作线程已全部退出，无并发访问
    std::lock_guard const lock(m_queueLock);
    for (auto &queue : m_queues) {
        queue.clear();
    }
}

END_NS_BACKEND
