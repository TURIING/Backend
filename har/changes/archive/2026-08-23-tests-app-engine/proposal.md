# Proposal: tests 的 App/Engine 测试引擎，让 CommandStream 闭环真实跑起来

## Why

`port-command-stream` 已交付 CommandStream 全套基础设施（`CommandStream`/`Dispatcher`/`Driver` 接口/`Handle`），其 gtest 单测验证过"记录 → Flush → 执行 → 归还"闭环。但工作区中该测试（`tests/CommandStreamTest.cpp`）已被移除、gtest 链接被摘除，取而代之的 `App.h`/`Engine.h`/`Engine.cpp`/`Macro.h` 全是未实现空壳，`tests/main.cpp` 为空——**当前 `BackendTests` 跑不起来 CommandStream 流程**。需要把这些文件实现为"最小应用"形态：`App` 单例跑帧循环，`Engine` 内部走 Vulkan 平台真实初始化（MoltenVK）并起执行线程消费命令，以可观察的方式跑通闭环，同时为后续 `VulkanDriver` 真实实现提供可运行的宿主。

## What Changes

- `tests/Engine.h`/`Engine.cpp`：从空壳到完整实现——Builder 模式（`BuilderDetails` + `Build()`）、记录端 API 转发到 `CommandStream`、`init()`（`PlatformFactory::Create(VULKAN)` → `CreateDriver` 真实 Vulkan 初始化 → `CommandBufferQueue` → `CommandStream` → 执行线程）、`Terminate()` 退出协议（`RequestExit` → `join` → 同步 `terminate`）
- `tests/App.cpp`（新增）：`App::Instance()` 单例与 `Run(setup, cleanup)` 帧循环
- `tests/main.cpp`：补 `main()` 入口，调用 `App::Instance().Run(...)`
- `src/vulkan/VulkanDriver.cpp`（微改）：`createFenceS()` 返回自增 id 句柄，使 RETURN 路径在无 gtest 下可观察
- 构建：无 CMake 结构性改动（`file(GLOB_RECURSE)` 自动收集新增 `App.cpp`）

非 BREAKING：`Backend` 库接口不变；`tests/` 仅是测试宿主形态变化。

## Capabilities

### New Capabilities

- `test-engine`: tests/ 的 App/Engine 测试引擎——Builder 构建、Vulkan 真实初始化、双线程 CommandStream 闭环（记录线程帧循环 + 执行线程消费）、退出协议

### Modified Capabilities

- 无（`command-stream`/`driver-interface`/`dispatcher`/`handle` 语义不变，仅新增调用方）

## Impact

- `tests/Engine.h`（重写：空壳 → Builder 公开 API + 记录方法声明 + 成员）
- `tests/Engine.cpp`（重写：`BuilderDetails`/`Build`/`init`/帧方法/`Terminate`/执行线程）
- `tests/App.cpp`（新增：`App::Instance`/`Run`）
- `tests/main.cpp`（修改：空 main → App 入口）
- `tests/CMakeLists.txt`（已改为无 gtest 形态，无需再动；验证 `std::thread` 链接即可）
- `src/vulkan/VulkanDriver.cpp`（微改：`createFenceS` 自增 id）
- 构建：根 CMake 与 tests CMake 的 GLOB 自动收集新源文件
