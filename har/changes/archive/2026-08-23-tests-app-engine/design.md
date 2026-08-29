# Design: tests 的 App/Engine 测试引擎

## Context

`port-command-stream` 已交付 CommandStream 全套基础设施且经 gtest 单测验证通过（`eb59fc6`）。工作区未提交改动将其移除：`tests/CommandStreamTest.cpp` 被删、`tests/CMakeLists.txt` 摘除 gtest/Threads 链接、`tests/main.cpp` 变空，并新增未实现的 `App.h`/`Engine.h`/`Engine.cpp`/`Macro.h` 骨架。当前 `BackendTests` 无法跑通 CommandStream 流程。

探索阶段确认的现状与事实：

- `CommandStream`/`Dispatcher`/`CommandStreamDispatcher`/`CommandBufferQueue`/`Command.h` 完整可用；`Driver` 抽象接口 + `DriverAPI.inc` 8 条种子方法就位
- `VulkanPlatform`（含 `VulkanPlatformApple`）是真实实现：`CreateDriver()` 触发 `volkInitialize` → `VulInstance` → `VulPhysicalDevice` → `VulLogicDevice` → `VulQueue` 全套真实初始化，日志打印物理设备/驱动信息；`VulkanPlatformPrivate` 成员逆序析构保证 device 先于 instance 销毁
- `VulkanDriver` 仍是占位：`Create()` 丢弃 platform/context 返回空驱动，仅 override `GetDispatcher()`/`terminate()`/`createFenceS()`（返回空句柄 `{}`）；命令回放调用的异步方法落在 `Driver` 基类非虚空实现上——机制真实、GPU 操作为空，符合占位现状
- `PlatformFactory::Create(VULKAN)` 在 `BACKEND_SUPPORT_VULKAN` + `PLATFORM_APPLE`（均定义于 `Utils/Macro.h`）下返回 `VulkanPlatformApple`；无窗口环境创建实例/物理设备/逻辑设备不需要 surface，可行
- 用户决策：① 不用 gtest，App/Engine 形态 ② 驱动走 Vulkan 平台真实初始化 ③ 双线程执行（执行线程 + `RequestExit`）

## Goals / Non-Goals

**Goals:**
- `tests/Engine.h`/`Engine.cpp` 实现：Builder 模式（`BuilderDetails` + `Build()`）、记录端 API 转发、`init()` 装配（平台/驱动/队列/流/执行线程）、`Terminate()` 退出协议
- `tests/App.cpp` 新增：`App::Instance()` 单例 + `Run(setup, cleanup)` 帧循环
- `tests/main.cpp` 补 `main()` 入口，`BackendTests` 运行后以可观察方式跑通闭环并干净退出
- `VulkanDriver::createFenceS()` 微改为自增 id，RETURN 路径可观察
- 构建与运行验证：`BackendTests` 编译通过、运行输出闭环日志、退出码 0

**Non-Goals:**
- 不实现 `VulkanDriver` 真实驱动逻辑（仍为占位，本变更只微改 `createFenceS` 返回值）
- 不引入 gtest/其他测试框架
- 不改 `CommandStream`/`CommandBufferQueue`/`Command.h`/`Driver.h`/`DriverAPI.inc` 既有代码
- 不移植 Filament 的完整 Engine（surface/swapchain/资源句柄池等均不涉及）
- 不改 `tests/CMakeLists.txt`（现形态已可用，仅验证线程链接）

## Decisions

### D1: 变更归属与文件布局

新建独立变更 `tests-app-engine`（`port-command-stream` 已交付完毕，其规划制品保持不动）。`tests/App.cpp` 为新增文件（`App::Instance`/`Run` 当前只有声明无定义，链接必失败），tests CMake 的 `file(GLOB_RECURSE)` 自动收集，无需改构建。`Macro.h` 已完整（`backend_test` 命名空间），不动。

### D2: 驱动选择（用户决策 2）

`Engine::init()` 走 `PlatformFactory::Create(BackendType::VULKAN)` → `platform->CreateDriver(config, nullptr)`，触发 MoltenVK 真实初始化。`Engine` 持有 `PlatformPtr`（保活 Vulkan 对象，析构时由 `VulkanPlatformPrivate` 逆序析构释放 device/instance）。初始化失败（`volkInitialize` 失败、平台为空、驱动为空）→ `LOG_CRITICAL` + `LOG_ASSERT(false)` 中止，不继续空跑。`DriverConfig` 默认构造（立体渲染关闭、默认优先级），`shareContext` 传 `nullptr`。

### D3: 执行模型（用户决策 3）

双线程：`init()` 所在线程（main）为记录线程，`std::thread` 为执行线程。执行线程循环：`WaitForCommands()` → 区间为空且 `IsExitRequested()` 则 break → 否则逐区间 `Execute(r.begin)` + `ReleaseBuffer(r)`。`Terminate()` 协议：`Flush()` → `RequestExit()` → `join()` → `driver->terminate()`；`RequestExit → join` 本身是"命令全部执行完"的同步屏障（`WaitForCommands` 在退出请求下仍返回残余区间执行完再退出）。`~Engine()` 兜底：若未 `Terminate()` 先执行退出协议，再释放成员（Ref 基类禁拷贝/移动，值成员 `CommandBufferQueue` 与 `CommandStreamPtr` 顺序安全）。

### D4: Engine 接口形态

Builder 模式照 `VulInstance::Builder` 先例：`struct BuilderDetails;` 前置声明于头文件、定义于 cpp，`Builder : BuilderBase<BuilderDetails>` 且 `friend struct Engine::BuilderDetails`；`BuilderDetails` 持 `BackendType`/`DriverConfig`/`requiredSize`/`bufferSize`（默认 1 MiB/2 MiB，沿用 gtest 参数）。`Engine` 构造私有，成员声明顺序：`m_platform` → `m_driver` → `m_queue`（值成员，不可移动）→ `m_stream`（`CommandStreamPtr`，构造需 `m_queue.GetCircularBuffer()` 引用，故必须晚于 `m_queue`）→ `m_thread`。`init()` 中 `m_stream->DebugThreading()` 校准记录线程（构造线程即 main，debug 断言天然成立）。记录端 API 为薄转发壳（PascalCase），宏展开的 `beginFrame` 等小写名为既定拼写例外（D3 of port-command-stream）。

### D5: 验证策略（无 gtest）

| 验证点 | 手段 |
|---|---|
| Vulkan 真实初始化 | 平台既有日志（`Selected physical device ...` / 驱动信息） |
| 闭环执行 | `setup` 里 `QueueCommand` 探针 lambda 打印日志，执行线程运行即证明记录→传输→回放 |
| RETURN 路径 | `CreateFence()` 打印自增 id（依赖 D6） |
| 记录线程断言 | `CommandStream::AllocateCommand` 的 debug `LOG_ASSERT(threadId)` |
| 干净退出 | `Terminate()` 后进程退出码 0，无挂起/崩溃 |

### D6: createFenceS 微改

`src/vulkan/VulkanDriver.cpp` 的 `createFenceS()` 从 `return {};` 改为 `static uint32_t sNextFenceId; return FenceHandle(sNextFenceId++);`——一行语义变化，使 RETURN 路径返回值可观察（spec: 句柄可观察场景）。`HandleBase(HandleId)` 构造要求 id 非 `kNullId`，自增从 0 起满足。

### D7: 帧循环与线程链接

`App::Run`：`Build` → `setup(engine)` → 8 帧（`BeginFrame(0, 16666, frame)` → `CreateFence` → `DestroyFence` → `Flush`，背压自动限速）→ `cleanup(engine)` → `Terminate()`。macOS 上 `std::thread` 通常无需显式 pthread 链接；构建 gate 若失败，恢复 tests CMake 的 `find_package(Threads)` + `Threads::Threads`。

## Risks / Trade-offs

- [`volkInitialize` 失败只 `LOG_CRITICAL` 不返回，空驱动继续跑] → `init()` 显式检查平台/驱动非空，失败即断言中止
- [执行线程生命周期泄漏（析构时未 join）] → `~Engine()` 兜底执行退出协议
- [`createFenceS` 自增 id 与 HandleBase 的 `kNullId` 断言冲突] → 自增从 0 起且 `uint32_t` 递增，远未触及 `UINT32_MAX` 前结束进程
- [macOS `std::thread` 链接失败] → 恢复 `Threads::Threads`（已列入构建 gate）
- [真实 Vulkan 初始化在无 GPU/无 MoltenVK 环境失败] → 失败中止路径已设计；本机为 Apple 平台 + MoltenVK 可用（`VK_USE_PLATFORM_METAL_EXT` 已启用）
- [记录线程与执行线程对 `VulkanDriver` 的并发访问] → 驱动为占位（方法空实现），无共享状态，无并发问题；真实驱动实现时由 `Execute()` 钩子负责上下文

## Open Questions

- 无（三个决策点已由用户确认：App/Engine 形态、Vulkan 真实初始化、双线程执行；帧数/默认 buffer 为实现细节）
