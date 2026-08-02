## 1. spdlog 依赖引入

- [x] 1.1 在 Utils 仓库中添加 spdlog submodule: `git submodule add https://github.com/gabime/spdlog.git 3rd/spdlog`
- [x] 1.2 更新 `CMakeLists.txt`: 添加 `add_subdirectory(3rd/spdlog)` 和 `target_link_libraries(Utils PUBLIC spdlog::spdlog)`

## 2. Log 头文件实现

- [x] 2.1 创建 `include/Utils/Log.h`: 定义 `LogLevel` 枚举类、`Log` 单例类声明（6 个模板方法、Init/Shutdown/Flush/SetLevel）、6 个 `LOG_*` 宏

## 3. Log 实现文件

- [x] 3.1 创建 `src/Log.cpp`: 实现 `Init()`（含平台 sink 分发：Android `android_sink_mt` / Windows `wincolor_stdout_sink_mt` / macOS `stdout_color_sink_mt`）、`Shutdown()`、`Flush()`、`SetLevel()`

## 4. 集成到 Utils 模块

- [x] 4.1 修改 `include/Utils/Utils.h`: 添加 `#include "Log.h"`
- [x] 4.2 在 Utils 仓库提交并推送更改

## 5. 测试

- [x] 5.1 创建 `tests/LogTest.cpp`: 测试 Init/Shutdown 生命周期、6 级日志输出、SetLevel 过滤、重复 Init 安全、Flush 不崩溃
- [x] 5.2 在 macOS 上构建并运行测试验证

## 6. 更新 Backend 引用

- [x] 6.1 在 Backend 仓库中更新 Utils submodule 引用到新 commit
- [x] 6.2 验证 Backend 构建通过
