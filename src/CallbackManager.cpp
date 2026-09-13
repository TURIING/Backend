#include "CallbackManager.h"

#include "DriverBase.h"

#include "Utils/Log.h"

BEGIN_NS_BACKEND

CallbackManager::CallbackManager(DriverBase &driver) : m_driver(driver), m_callbacks(1) {}

CallbackManager::~CallbackManager() noexcept = default;

void CallbackManager::Terminate() noexcept {
    std::lock_guard const lock(m_lock);
    for (auto &item : m_callbacks) {
        if (item.func) {
            m_driver.ScheduleCallback(item.handler, item.user, item.func);
        }
    }
}

CallbackManager::Handle CallbackManager::Get() const noexcept {
    Container::const_iterator const curr = GetCurrent();
    curr->count.fetch_add(1);
    return curr;
}

void CallbackManager::Put(Handle &curr) noexcept {
    if (curr->count.fetch_sub(1) == 1) {
        if (curr->func) {
            m_driver.ScheduleCallback(curr->handler, curr->user, curr->func);
            DestroySlot(curr);
        }
    }
    curr = {};
}

void CallbackManager::SetCallback(CallbackHandler *handler, CallbackHandler::Callback func, void *user) {
    LOG_ASSERT(func != nullptr);

    Container::iterator const curr = AllocateNewSlot();
    curr->handler                  = handler;
    curr->func                     = func;
    curr->user                     = user;
    if (curr->count == 0) {
        m_driver.ScheduleCallback(curr->handler, curr->user, curr->func);
        DestroySlot(curr);
    }
}

END_NS_BACKEND
