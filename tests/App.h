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
};
END_NS_TEST