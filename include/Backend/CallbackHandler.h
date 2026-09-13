#pragma once

#include "Backend/DriverDefine.h"

BEGIN_NS_BACKEND

/**
 * 回调派发接口，由 app 侧实现。
 *
 * 生命周期：后端只持有裸指针并调用 Post，**不拥有其生命周期**。调用方须保证 handler
 * 在所有待派发回调执行完毕前保持存活；Post 实现须线程安全（后端会从 ServiceThread 调用）。
 */
class CallbackHandler {
public:
    using Callback = void (*)(void* user);

    virtual void Post(void* user, Callback callback) = 0;

protected:
    virtual ~CallbackHandler() = default;
};

END_NS_BACKEND
