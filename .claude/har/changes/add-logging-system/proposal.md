## Why
当前 Backend 项目缺少统一的日志系统，调试和问题排查依赖 `printf` 或平台特定 API，无法统一控制日志级别和输出格式。需要为 Utils 模块引入基于 spdlog 的异步日志系统，为所有上层模块提供一致的日志能力。

## What Changes
- Utils 模块新增 `spdlog` 第三方库（git submodule）
- 新增 `Log` 类（单例），提供 6 级异步日志
- 提供 `LOG_TRACE / LOG_DEBUG / LOG_INFO / LOG_WARN / LOG_ERROR / LOG_CRITICAL` 宏接口
- 按平台自动选择 sink：Android → logcat，Windows → WinColor 控制台，macOS → ANSI 彩色终端

## Capabilities

### New Capabilities
- `add-logging`: 为 Utils 模块添加基于 spdlog 的异步日志系统，支持多平台 sink 自动选择

## Impact
- 受影响的模块: `3rd/Utils`（独立 git 仓库，需在其仓库内修改）
- 新增文件: `include/Utils/Log.h`、`src/Log.cpp`、`tests/LogTest.cpp`
- 修改文件: `CMakeLists.txt`（添加 spdlog submodule 和链接）、`include/Utils/Utils.h`（聚合 Log.h）
- 新增依赖: spdlog（header-only，通过 git submodule 获取）
- 对 Backend 上层透明: 只需 `target_link_libraries(... Utils)` 即可获得日志能力
