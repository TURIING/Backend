#pragma once
#include <functional>

#include "Engine.h"
#include "Macro.h"

BEGIN_NS_TEST

class App {
public:
    using SetupCallback   = std::function<void(const EnginePtr&)>;
    using CleanUpCallback = std::function<void(const EnginePtr&)>;

    static App& Instance();
    void        Run(const SetupCallback& setupCallback, const CleanUpCallback& cleanupCallback);

    // 打开一个 GLFW 窗口，在其 CAMetalLayer 上创建交换链并持续渲染三角形直到窗口关闭
    void        RunWindowed(uint32_t width, uint32_t height);
};
END_NS_TEST