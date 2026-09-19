#include "Utils/Log.h"

#include <cstring>

#include "App.h"
#include "Engine.h"

using namespace backend_test;

namespace {

constexpr uint32_t kWindowWidth  = 960;
constexpr uint32_t kWindowHeight = 640;

}  // namespace

int main(int argc, char** argv) {
    utils::Log::Instance().Init();

    bool const windowed = argc > 1 && std::strcmp(argv[1], "--window") == 0;
    if (true) {
        App::Instance().RunWindowed(kWindowWidth, kWindowHeight);
    } else {
        auto setup = [](const EnginePtr& engine) { engine->QueueCommand([] { LOG_INFO("probe: executed on driver thread"); }); };
        auto clean = [](const EnginePtr&) { LOG_INFO("cleanup: before terminate"); };

        App::Instance().Run(setup, clean);
    }

    utils::Log::Instance().Shutdown();
    return 0;
}
