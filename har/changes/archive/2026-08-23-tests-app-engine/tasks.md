# Tasks: tests 的 App/Engine 测试引擎

依赖顺序：Engine 头 → Engine 实现 → App → main 入口 → createFenceS 微改 → 构建 gate → 运行验证。

## 1. Engine 接口补全

- [x] 1.1 `tests/Engine.h`：`struct BuilderDetails;` 前置声明；`Builder` 补公开 API（`BackendType(BackendType)`、`DriverConfig(DriverConfig const&)`、`BufferSize(size_t required, size_t bufferSize)`、`EnginePtr Build()`）并 `friend struct Engine::BuilderDetails`；`Engine` 补记录方法声明（`BeginFrame`/`Flush`/`Finish`/`CreateFence`/`DestroyFence`/`ResetState`/`Terminate`/`QueueCommand`）；私有构造函数（`BackendType`、`DriverConfig`、buffer 大小参数）；成员声明顺序 `m_platform` → `m_driver` → `m_queue` → `m_stream` → `m_thread`（`m_queue` 值成员必须先于 `m_stream`，其构造依赖 `m_queue.GetCircularBuffer()`）
- [x] 1.2 `tests/Engine.cpp`：`struct Engine::BuilderDetails`（`BackendType`/`DriverConfig`/`requiredSize`/`bufferSize`，默认 1 MiB/2 MiB）；`Builder` 各 setter 与 `Build()`（构造 `Engine` 并返回 `EnginePtr`）；`Engine` 构造函数初始化成员

## 2. Engine 核心实现

- [x] 2.1 `tests/Engine.cpp` 实现 `init()`：`PlatformFactory::Create(BackendType::VULKAN)` → 判空；`platform->CreateDriver(config, nullptr)` → 判空；构造 `CommandBufferQueue` 与 `CommandStream`（`DriverPtr` + `queue.GetCircularBuffer()`）；`m_stream->DebugThreading()`；启动执行线程（`WaitForCommands` → 空且 `IsExitRequested` 则 break → `Execute` + `ReleaseBuffer`）
- [x] 2.2 `tests/Engine.cpp` 实现记录端转发：`BeginFrame`/`Flush`/`Finish`/`ResetState`/`QueueCommand` 转 `m_stream`；`CreateFence` 转 `m_stream->createFence()`；`DestroyFence` 转 `m_stream->destroyFence(fh)`
- [x] 2.3 `tests/Engine.cpp` 实现 `Terminate()`：`Flush()` → `RequestExit()` → `join()` → `driver->terminate()`；`~Engine()` 兜底（未 `Terminate` 先执行退出协议）

## 3. App 与入口

- [x] 3.1 新增 `tests/App.cpp`：`App::Instance()` 返回静态单例；`Run(setup, cleanup)`：`Engine::Builder().BackendType(BackendType::VULKAN).Build()` → `setup(engine)` → 8 帧（`BeginFrame(0, 16666, frame)` → `CreateFence` → 打印句柄 id → `DestroyFence` → `Flush`）→ `cleanup(engine)` → `engine->Terminate()`
- [x] 3.2 `tests/main.cpp`：`main()` 调 `App::Instance().Run(setup, cleanup)`；`setup` 中 `QueueCommand` 探针 lambda 打印"executed on driver thread"日志；返回 0

## 4. RETURN 路径可观察性

- [x] 4.1 `src/vulkan/VulkanDriver.cpp`：`createFenceS()` 从 `return {};` 改为返回自增 id 句柄（静态计数器从 0 递增，`FenceHandle(id++)`）

## 5. 构建与运行验证

- [x] 5.1 构建 `BackendTests` 通过（含新增 `App.cpp`，GLOB 自动收集）；若 `std::thread` 链接失败，恢复 tests CMake 的 `find_package(Threads)` + `Threads::Threads`
- [x] 5.2 运行 `BackendTests`：验证输出——Vulkan 物理设备/驱动初始化日志、每帧 fence id 递增（0..7）、探针 lambda 日志、进程退出码 0、无挂起
- [x] 5.3 无 gtest 链接残留：`BackendTests` 不依赖 gtest_main，`tests/CMakeLists.txt` 保持现形态
