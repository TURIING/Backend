#pragma once

#include "Backend/DriverDefine.h"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

BEGIN_NS_BACKEND

// 编译任务的身份标识：Program 对象经 shared_ptr 与其任务绑定，
// Program 销毁后 token 失效，排队中的任务可据此被丢弃
struct ProgramToken {
    virtual ~ProgramToken();
};

using ProgramTokenPtr = std::shared_ptr<ProgramToken>;

// 固定线程数的编译工作池：按优先级从三条队列取任务执行。
//
// 队列下标即 CompilerPriorityQueue 的取值，高优先级队列非空时低优先级任务让位。
// Queue/Dequeue 可从任意线程调用，任务体在锁外执行。
class CompilerThreadPool {
public:
    using Job           = std::function<void()>;
    using ThreadSetup   = std::function<void()>;
    using ThreadCleanup = std::function<void()>;

    CompilerThreadPool() noexcept;
    ~CompilerThreadPool() noexcept;

    // threadSetup / threadCleanup 在工作线程启动与退出时执行（用于设置线程名与优先级）
    void Init(uint32_t threadCount, ThreadSetup&& threadSetup, ThreadCleanup&& threadCleanup);

    void Terminate() noexcept;

    void Queue(CompilerPriorityQueue priorityQueue, ProgramTokenPtr const& token, Job&& job);

    // 取出并移除某 token 的任务；不存在时返回空 Job
    NODISCARD Job Dequeue(ProgramTokenPtr const& token);

private:
    using TaskQueue = std::deque<std::pair<ProgramTokenPtr, Job>>;

    // 调用方须持 m_queueLock
    std::pair<TaskQueue&, TaskQueue::iterator> Find(ProgramTokenPtr const& token);

    std::vector<std::thread> m_compilerThreads;

    bool                    m_exitRequested = false;
    mutable std::mutex      m_queueLock;
    std::condition_variable m_queueCondition;
    std::array<TaskQueue, COMPILER_PRIORITY_QUEUE_COUNT> m_queues;
};

END_NS_BACKEND
