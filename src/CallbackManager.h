#pragma once

#include "Backend/CallbackHandler.h"

#include <atomic>
#include <list>
#include <mutex>

BEGIN_NS_BACKEND

class DriverBase;

// 回调调度器：以「条件」计数决定何时触发回调。
//
// Get() 创建一个条件并返回句柄，Put() 满足它 —— 两者通常来自不同线程。
// SetCallback() 原子地登记回调与一组新条件：若此前创建的条件已全部满足，回调立即被调度。
class CallbackManager {
    struct Callback {
        mutable std::atomic_int   count{};
        CallbackHandler*          handler = nullptr;
        CallbackHandler::Callback func    = {};
        void*                     user    = nullptr;
    };

    using Container = std::list<Callback>;

public:
    using Handle = Container::const_iterator;

    explicit CallbackManager(DriverBase& driver);

    ~CallbackManager() noexcept;

    // 关闭时把全部待决回调立即派发出去，避免资源泄漏；此时条件是否满足已不重要
    void Terminate() noexcept;

    NODISCARD Handle Get() const noexcept;

    // 满足一个条件；若此前登记过回调且这是最后一个未满足的条件，则调度该回调
    void Put(Handle& curr) noexcept;

    void SetCallback(CallbackHandler* handler, CallbackHandler::Callback func, void* user);

private:
    NODISCARD Container::const_iterator GetCurrent() const noexcept {
        std::lock_guard const lock(m_lock);
        return --m_callbacks.end();
    }

    Container::iterator AllocateNewSlot() {
        std::lock_guard const lock(m_lock);
        auto                  curr = --m_callbacks.end();
        m_callbacks.emplace_back();
        return curr;
    }

    void DestroySlot(Container::const_iterator curr) noexcept {
        std::lock_guard const lock(m_lock);
        m_callbacks.erase(curr);
    }

    DriverBase&        m_driver;
    mutable std::mutex m_lock;
    Container          m_callbacks;
};

END_NS_BACKEND
