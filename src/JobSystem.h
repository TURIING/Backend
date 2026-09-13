#pragma once

#include "Backend/DriverDefine.h"

#include "Utils/Macro.h"

BEGIN_NS_BACKEND

// 仅提供线程命名与优先级设置：上游 Vulkan 后端对 JobSystem 的使用除此之外为零，
// 故不移植其工作窃取调度器
class JobSystem {
public:
    enum class Priority { Low, Normal, Display };

    static void SetThreadName(char const* name) noexcept;
    static void SetThreadPriority(Priority priority) noexcept;
};

END_NS_BACKEND
