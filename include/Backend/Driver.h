#pragma once

#include <functional>

#include "DriverDefine.h"
#include "Handle.h"

BEGIN_NS_BACKEND

class Dispatcher;

// 异步方法非虚、经命令流排队，同步方法纯虚、直接调用，避免记录端虚调用
class Driver : public NS_UTILS::Ref {
public:
    virtual ~Driver() noexcept = default;

    // 命令流构造时调用一次，返回驱动方法派发表
    virtual Dispatcher GetDispatcher() const noexcept = 0;

    // 命令批执行钩子，默认直接执行；驱动可借此包装（如 GPU 上下文切换）
    virtual void Execute(std::function<void()> const& fn) { fn(); }

    NODISCARD static size_t GetElementTypeSize(ElementType type) noexcept;

#undef DECL_DRIVER_API
#define DECL_DRIVER_API(methodName, paramsDecl, params) \
    void methodName(paramsDecl) {}

#undef DECL_DRIVER_API_SYNCHRONOUS
#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params) virtual RetType methodName(paramsDecl) = 0;

#undef DECL_DRIVER_API_RETURN
#define DECL_DRIVER_API_RETURN(RetType, methodName, paramsDecl, params) \
    virtual RetType methodName##S() noexcept = 0;                       \
    void            methodName##R(RetType, paramsDecl) {}

#include "Backend/DriverAPI.inc"
};

DECLARE_SHARE_PTR_CLASS(Driver);

END_NS_BACKEND
