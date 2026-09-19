#pragma once

#include <chrono>
#include <functional>
#include <type_traits>

#include "Backend/BufferDescriptor.h"
#include "Backend/CallbackHandler.h"
#include "Backend/DriverDefine.h"
#include "Backend/Handle.h"
#include "Backend/PixelBufferDescriptor.h"
#include "Backend/Program.h"
#include "Backend/TargetBufferInfo.h"

#include "Utils/math/Matrix.h"

BEGIN_NS_BACKEND

class CommandStream;
class Dispatcher;

using DriverApi = CommandStream;  // 命令流别名：驱动接口签名的书写形式（与上游 DriverApiForward.h 一致）

// 异步方法非虚、经命令流排队，同步方法纯虚、直接调用，避免记录端虚调用
class Driver : public NS_UTILS::Ref {
public:
    virtual ~Driver() noexcept = default;

    // 命令流构造时调用一次，返回驱动方法派发表
    virtual Dispatcher GetDispatcher() const noexcept = 0;

    // 命令批执行钩子，默认直接执行；驱动可借此包装（如 GPU 上下文切换）
    virtual void Execute(std::function<void()> const& fn) { fn(); }

    // 由主线程（非渲染线程）周期性调用，驱动在此执行用户回调
    virtual void Purge() noexcept = 0;

    // 由渲染线程调用：handler 非空时经其派发，为空时留给 Purge() 在主线程执行
    virtual void ScheduleCallback(CallbackHandler* handler, void* user, CallbackHandler::Callback callback) = 0;

    // 标记驱动遇到不可恢复错误：中断全部待决的围栏等待，且阻止后续等待
    virtual void SetUnrecoverableError() noexcept {}

    // 仅在调试构建或手动开启时被调用
    virtual void DebugCommandBegin(CommandStream* cmds, bool synchronous, char const* methodName) noexcept = 0;
    virtual void DebugCommandEnd(CommandStream* cmds, bool synchronous, char const* methodName) noexcept = 0;

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
