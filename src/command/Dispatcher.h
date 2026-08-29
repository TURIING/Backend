#pragma once

#include "Backend/DriverDefine.h"

#include <cstdint>

BEGIN_NS_BACKEND

class CommandBase;
class Driver;

// 命令对象持有表中的函数指针执行调用，使命令流与具体驱动解耦
class Dispatcher {
public:
    using Execute = void (*)(Driver& driver, CommandBase* self, intptr_t* next);

#undef DECL_DRIVER_API_SYNCHRONOUS
#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params)
#undef DECL_DRIVER_API
#define DECL_DRIVER_API(methodName, paramsDecl, params) Execute methodName##_;
#undef DECL_DRIVER_API_RETURN
#define DECL_DRIVER_API_RETURN(RetType, methodName, paramsDecl, params) Execute methodName##_;

#include "Backend/DriverAPI.inc"
};

END_NS_BACKEND
