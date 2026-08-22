#pragma once

#include "Backend/Driver.h"
#include "CommandStream.h"

#include <cstddef>
#include <cstdint>
#include <utility>

#undef DECL_DRIVER_API
#undef DECL_DRIVER_API_SYNCHRONOUS
#undef DECL_DRIVER_API_RETURN

BEGIN_NS_BACKEND

// 为具体驱动生成派发表，供 GetDispatcher() 返回
template <typename ConcreteDriver>
class ConcreteDispatcher {
public:
    static Dispatcher Make() noexcept;

private:
#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params)
#define DECL_DRIVER_API(methodName, paramsDecl, params)                         \
    static void methodName(Driver& driver, CommandBase* base, intptr_t* next) { \
        using Cmd                      = COMMAND_TYPE(methodName);              \
        ConcreteDriver& concreteDriver = static_cast<ConcreteDriver&>(driver);  \
        Cmd::Execute(&ConcreteDriver::methodName, concreteDriver, base, next);  \
    }
#define DECL_DRIVER_API_RETURN(RetType, methodName, paramsDecl, params)           \
    static void methodName(Driver& driver, CommandBase* base, intptr_t* next) {   \
        using Cmd                      = COMMAND_TYPE(methodName##R);             \
        ConcreteDriver& concreteDriver = static_cast<ConcreteDriver&>(driver);    \
        Cmd::Execute(&ConcreteDriver::methodName##R, concreteDriver, base, next); \
    }
#include "Backend/DriverAPI.inc"
};

template <typename ConcreteDriver>
Dispatcher ConcreteDispatcher<ConcreteDriver>::Make() noexcept {
    Dispatcher dispatcher;

#undef DECL_DRIVER_API_SYNCHRONOUS
#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params)
#undef DECL_DRIVER_API
#define DECL_DRIVER_API(methodName, paramsDecl, params) dispatcher.methodName##_ = &ConcreteDispatcher::methodName;
#undef DECL_DRIVER_API_RETURN
#define DECL_DRIVER_API_RETURN(RetType, methodName, paramsDecl, params) \
    dispatcher.methodName##_ = &ConcreteDispatcher::methodName;

#include "Backend/DriverAPI.inc"

    return dispatcher;
}

END_NS_BACKEND
