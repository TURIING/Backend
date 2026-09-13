#pragma once

#include "Backend/Driver.h"
#include "Backend/CallbackHandler.h"

#include "Utils/Compiler.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

BEGIN_NS_BACKEND

// 所有 Driver 实现的基类。
//
// 持有一个 ServiceThread：用户回调不直接在渲染线程执行，而是投递给该线程，再由它调用
// CallbackHandler::Post —— 这保证回调派发不阻塞渲染线程，且永不落在主线程上。
// 传入空 handler 时回调改入 purge 队列，由主线程周期性调用 Purge() 执行。
class DriverBase : public Driver {
public:
    explicit DriverBase(DriverConfig const& driverConfig) noexcept;
    ~DriverBase() noexcept override;

    void Purge() noexcept final;

    // 回调的可调用体在 8 个指针的内联存储里就地构造，避免为一次性投递分配堆内存
    struct CallbackData {
        static CallbackData* Obtain(DriverBase* allocator);
        static void Release(CallbackData* data);

        CallbackData(CallbackData const&)            = delete;
        CallbackData(CallbackData&&)                 = delete;
        CallbackData& operator=(CallbackData const&) = delete;
        CallbackData& operator=(CallbackData&&)      = delete;

        void* storage[8] = {};

    protected:
        CallbackData() = default;
    };

    template <typename T>
    void ScheduleCallback(CallbackHandler* handler, T&& functor) {
        static_assert(sizeof(T) <= sizeof(CallbackData::storage), "functor too large");

        CallbackData* data = CallbackData::Obtain(this);
        new (data->storage) T(std::forward<T>(functor));
        ScheduleCallback(handler, data, static_cast<CallbackHandler::Callback>([](void* raw) {
                             auto* details = static_cast<CallbackData*>(raw);
                             T&    functor = *static_cast<T*>(static_cast<void*>(details->storage));
                             functor();
                             functor.~T();
                             CallbackData::Release(details);
                         }));
    }

    void ScheduleCallback(CallbackHandler* handler, void* user, CallbackHandler::Callback callback) final;

    // 等待谓词成立或超时；驱动遇到不可恢复错误时提前返回 Error
    template <typename Predicate>
    FenceStatus WaitForFence(Predicate predicate, std::chrono::steady_clock::time_point until) {
        std::unique_lock lock(m_fenceMutex);
        bool             errorObserved = false;
        bool             timeout       = false;
        while (!predicate() && !(errorObserved = m_hasUnrecoverableError.load(std::memory_order_relaxed))) {
            if (m_fenceCondition.wait_until(lock, until) == std::cv_status::timeout) {
                timeout = true;
                break;
            }
        }
        if (errorObserved) {
            return FenceStatus::Error;
        }
        return !timeout ? FenceStatus::ConditionSatisfied : FenceStatus::TimeoutExpired;
    }

    template <typename Predicate>
    FenceStatus WaitForFence(Predicate predicate) {
        std::unique_lock lock(m_fenceMutex);
        bool             errorObserved = false;
        while (!predicate() && !(errorObserved = m_hasUnrecoverableError.load(std::memory_order_relaxed))) {
            m_fenceCondition.wait(lock);
        }
        if (errorObserved) {
            return FenceStatus::Error;
        }
        return FenceStatus::ConditionSatisfied;
    }

    // 执行动作并唤醒全部等待者
    template <typename Action>
    void SignalFence(Action action) {
        std::lock_guard const lock(m_fenceMutex);
        action();
        m_fenceCondition.notify_all();
    }

    void SetUnrecoverableError() noexcept final {
        std::lock_guard const lock(m_fenceMutex);
        m_hasUnrecoverableError.store(true, std::memory_order_relaxed);
        m_fenceCondition.notify_all();
    }

    NODISCARD bool HasUnrecoverableError() const noexcept { return m_hasUnrecoverableError.load(std::memory_order_relaxed); }

protected:
    NODISCARD DriverConfig const& GetDriverConfig() const noexcept { return m_driverConfig; }

    // 上游在 FILAMENT_DEBUG_COMMANDS 非默认值时输出命令追踪；项目未移植该体系，
    // 默认开关下上游行为同样为空，故此实现等价
    void DebugCommandBegin(CommandStream* cmds, bool synchronous, char const* methodName) noexcept override;
    void DebugCommandEnd(CommandStream* cmds, bool synchronous, char const* methodName) noexcept override;

    // 幂等；析构时自动调用，需要提前关闭 ServiceThread 时也可显式调用
    void StopServiceThread() noexcept;

private:
    DriverConfig const m_driverConfig;

    mutable std::mutex                                        m_purgeLock;
    std::vector<std::pair<void*, CallbackHandler::Callback>>  m_callbacks;

    std::thread                                               m_serviceThread;
    mutable std::mutex                                        m_serviceThreadLock;
    mutable std::condition_variable                           m_serviceThreadCondition;
    std::vector<std::tuple<CallbackHandler*, CallbackHandler::Callback, void*>> m_serviceThreadCallbackQueue;
    bool                                                      m_exitRequested = false;

    mutable std::mutex              m_fenceMutex;
    mutable std::condition_variable m_fenceCondition;
    std::atomic<bool>               m_hasUnrecoverableError{ false };
};

END_NS_BACKEND
