#include "Backend/Driver.h"

#include "DriverBase.h"
#include "JobSystem.h"

#include "Utils/Macro.h"

#include <new>
#include <utility>

BEGIN_NS_BACKEND

namespace {

class CallbackDataDetails final : public DriverBase::CallbackData {
public:
    explicit CallbackDataDetails(DriverBase *allocator) : m_allocator(allocator) {}

private:
    [[maybe_unused]] DriverBase *m_allocator;
};

}  // namespace

DriverBase::CallbackData *DriverBase::CallbackData::Obtain(DriverBase *allocator) { return new CallbackDataDetails(allocator); }

void DriverBase::CallbackData::Release(CallbackData *data) { delete static_cast<CallbackDataDetails *>(data); }

DriverBase::DriverBase(DriverConfig const &driverConfig) noexcept : m_driverConfig(driverConfig) {
#if UTILS_HAS_THREADING
    m_serviceThread = std::thread([this]() {
        JobSystem::SetThreadName("ServiceThread");

        decltype(m_serviceThreadCallbackQueue) callbacks;
        for (;;) {
            {
                std::unique_lock lock(m_serviceThreadLock);
                while (m_serviceThreadCallbackQueue.empty() && !m_exitRequested) {
                    m_serviceThreadCondition.wait(lock);
                }
                if (m_exitRequested) {
                    break;
                }
                callbacks.swap(m_serviceThreadCallbackQueue);
            }
            // 锁外投递：handler->Post 可能阻塞，不应持有队列锁
            for (auto &[handler, callback, user] : callbacks) {
                handler->Post(user, callback);
            }
            callbacks.clear();
        }
    });
#endif
}

DriverBase::~DriverBase() noexcept {
    LOG_ASSERT(m_callbacks.empty());
#if UTILS_HAS_THREADING
    StopServiceThread();
#endif
}

void DriverBase::ScheduleCallback(CallbackHandler *handler, void *user, CallbackHandler::Callback callback) {
    if (handler && UTILS_HAS_THREADING) {
        std::lock_guard const lock(m_serviceThreadLock);
        m_serviceThreadCallbackQueue.emplace_back(handler, callback, user);
        m_serviceThreadCondition.notify_one();
        return;
    }
    std::lock_guard const lock(m_purgeLock);
    m_callbacks.emplace_back(user, callback);
}

void DriverBase::Purge() noexcept {
    decltype(m_callbacks) callbacks;
    {
        std::lock_guard const lock(m_purgeLock);
        callbacks.swap(m_callbacks);
    }
    for (auto &item : callbacks) {
        item.second(item.first);
    }
}

void DriverBase::DebugCommandBegin(CommandStream *cmds, bool synchronous, char const *methodName) noexcept {
    (void)cmds;
    (void)synchronous;
    (void)methodName;
}

void DriverBase::DebugCommandEnd(CommandStream *cmds, bool synchronous, char const *methodName) noexcept {
    (void)cmds;
    (void)synchronous;
    (void)methodName;
}

void DriverBase::StopServiceThread() noexcept {
    if (!m_serviceThread.joinable()) {
        return;
    }
    {
        std::lock_guard const lock(m_serviceThreadLock);
        m_exitRequested = true;
    }
    m_serviceThreadCondition.notify_one();
    m_serviceThread.join();

    LOG_ASSERT(m_serviceThreadCallbackQueue.empty());
}

size_t Driver::GetElementTypeSize(ElementType type) noexcept {
    switch (type) {
        CASE_FROM_TO(ElementType::BYTE, 1);
        CASE_FROM_TO(ElementType::BYTE2, 2);
        CASE_FROM_TO(ElementType::BYTE3, 3);
        CASE_FROM_TO(ElementType::BYTE4, 4);
        CASE_FROM_TO(ElementType::UBYTE, 1);
        CASE_FROM_TO(ElementType::UBYTE2, 2);
        CASE_FROM_TO(ElementType::UBYTE3, 3);
        CASE_FROM_TO(ElementType::UBYTE4, 4);
        CASE_FROM_TO(ElementType::SHORT, 2);
        CASE_FROM_TO(ElementType::SHORT2, 4);
        CASE_FROM_TO(ElementType::SHORT3, 6);
        CASE_FROM_TO(ElementType::SHORT4, 8);
        CASE_FROM_TO(ElementType::USHORT, 2);
        CASE_FROM_TO(ElementType::USHORT2, 4);
        CASE_FROM_TO(ElementType::USHORT3, 6);
        CASE_FROM_TO(ElementType::USHORT4, 8);
        CASE_FROM_TO(ElementType::INT, 4);
        CASE_FROM_TO(ElementType::UINT, 4);
        CASE_FROM_TO(ElementType::FLOAT, 4);
        CASE_FROM_TO(ElementType::FLOAT2, 8);
        CASE_FROM_TO(ElementType::FLOAT3, 12);
        CASE_FROM_TO(ElementType::FLOAT4, 16);
        CASE_FROM_TO(ElementType::HALF, 2);
        CASE_FROM_TO(ElementType::HALF2, 4);
        CASE_FROM_TO(ElementType::HALF3, 6);
        CASE_FROM_TO(ElementType::HALF4, 8);
    }
    return 0;
}

END_NS_BACKEND
