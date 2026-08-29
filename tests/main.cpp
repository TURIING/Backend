#include "Utils/Log.h"

#include "App.h"
#include "Engine.h"

using namespace backend_test;

int main() {
    utils::Log::Instance().Init();
    auto setup = [](const EnginePtr& engine) {
        engine->QueueCommand([] { LOG_INFO("probe: executed on driver thread"); });
    };
    auto clean = [](const EnginePtr&) { LOG_INFO("cleanup: before terminate"); };

    App::Instance().Run(setup, clean);
    utils::Log::Instance().Shutdown();
    return 0;
}
