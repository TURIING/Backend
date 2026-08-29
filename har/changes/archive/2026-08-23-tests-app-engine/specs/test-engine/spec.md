# Capability: test-engine

## Purpose

tests/ 的 App/Engine 测试引擎：`Engine`（Builder 模式）内部以 Vulkan 平台真实初始化（MoltenVK）创建驱动，装配 `CommandBufferQueue` + `CommandStream` 并起执行线程，`App` 单例跑帧循环，让"记录 → Flush → 传输 → 回放 → 归还"闭环以最小应用形态真实运行；替代被移除的 gtest 单测作为 CommandStream 流程的运行宿主。

## Requirements

### Requirement: Engine Builder 构建

`Engine` SHALL 保持继承 `NS_UTILS::Ref` 并声明 `DECLARE_SHARE_PTR_CLASS(Engine)`（`EnginePtr`）；构造 SHALL 私有，`Builder` SHALL 继承 `NS_UTILS::BuilderBase<BuilderDetails>`（`BuilderDetails` 定义在 `Engine.cpp`，`Builder` 为其 friend，照 `VulInstance::Builder` 先例）；`Builder` SHALL 提供 `BackendType(BackendType)`、`DriverConfig(DriverConfig const&)`、`BufferSize(size_t required, size_t bufferSize)`（默认 1 MiB / 2 MiB）与 `Build()` 返回 `EnginePtr` 的公开 API。

#### Scenario: Builder 装配与构建

- **WHEN** 调用 `Engine::Builder().BackendType(BackendType::VULKAN).Build()`
- **THEN** 返回非空 `EnginePtr`，且未显式设置的配置取默认值（`DriverConfig` 默认构造、buffer 1 MiB/2 MiB）

#### Scenario: 构造私有

- **WHEN** 外部代码直接 `new Engine(...)`
- **THEN** 编译失败，只能经 `Builder::Build()` 创建

### Requirement: Engine Vulkan 真实初始化

`Engine::init()` SHALL 经 `PlatformFactory::Create(BackendType::VULKAN)` 创建平台，再调 `platform->CreateDriver(config, nullptr)` 触发真实 Vulkan 初始化（`volkInitialize` → 实例 → 物理设备选择 → 逻辑设备 → 队列），随后构造 `CommandBufferQueue`（requiredSize/bufferSize）与 `CommandStream`（`DriverPtr` + `queue.GetCircularBuffer()`），并调用 `DebugThreading()` 校准记录线程；任一环节失败（平台为空、驱动创建失败）SHALL `LOG_CRITICAL` 并 `LOG_ASSERT(false)` 中止，不继续空跑。

#### Scenario: 真实设备初始化

- **WHEN** `Engine::Builder().BackendType(BackendType::VULKAN).Build()`
- **THEN** 日志输出 MoltenVK 实例/物理设备/逻辑设备初始化信息，`Engine` 持有非空 `DriverPtr`

#### Scenario: 初始化失败中止

- **WHEN** 平台创建或驱动初始化失败（如无 MoltenVK/无 GPU）
- **THEN** 记录 `LOG_CRITICAL` 并触发断言，`Build()` 不返回可用 `EnginePtr`

### Requirement: Engine 双线程执行模型

`Engine` SHALL 在 `init()` 中启动执行线程，循环 `queue.WaitForCommands()`；对每个返回区间依次 `stream.Execute(r.begin)` 与 `queue.ReleaseBuffer(r)`；区间为空且 `queue.IsExitRequested()` SHALL 退出线程；记录线程（`init()` 所在线程）经 `CommandStream` 记录 API 写入命令，缓冲满时由 `CommandBufferQueue` 背压阻塞自动限速。

#### Scenario: 异步消费

- **WHEN** 记录线程写入命令并 `Flush()`
- **THEN** 执行线程在另一线程中按记录顺序回放命令并归还缓冲区，无需记录线程参与

#### Scenario: 退出协议

- **WHEN** `Terminate()` 依次执行 `Flush()` → `RequestExit()` → `join()` → `driver->terminate()`
- **THEN** 执行线程消费完残余命令后退出，`terminate()` 发生在所有命令之后，`join()` 返回后线程已终止

#### Scenario: 析构兜底

- **WHEN** `Engine` 析构时 `Terminate()` 尚未调用
- **THEN** 析构函数先执行退出协议（join 执行线程）再释放成员，不遗留运行中线程

### Requirement: Engine 记录端 API

`Engine` SHALL 将 `CommandStream` 记录方法转发为 PascalCase 公开方法：`BeginFrame(int64_t, int64_t, uint32_t)`、`Flush()`、`Finish()`、`CreateFence()`（RETURN 路径：同步调 `createFenceS` 取句柄 + 入队 `createFenceR`）、`DestroyFence(FenceHandle)`、`ResetState()`、`Terminate()`（同步直调）、`QueueCommand(std::function<void()>)`；转发 SHALL 保持异步方法排队、RETURN 方法同步取结果、同步方法直调的三条语义不变（沿用 `DriverAPI.inc` 宏展开路径）。

#### Scenario: 异步入队

- **WHEN** 记录线程调用 `BeginFrame(...)`/`Flush()`
- **THEN** 命令写入环形缓冲，执行前驱动对应方法不被调用

#### Scenario: RETURN 路径

- **WHEN** 调用 `CreateFence()`
- **THEN** 立即返回句柄（`createFenceS` 已同步执行），`createFenceR` 命令入队待执行

#### Scenario: 同步直调

- **WHEN** 调用 `Terminate()`
- **THEN** 立即调用 `Driver::terminate()`，缓冲无写入

### Requirement: App 单例运行

`App` SHALL 为单例（`App::Instance()` 返回唯一实例）；`Run(SetupCallback, CleanUpCallback)` SHALL：构建 `Engine` → 调 `setup(enginePtr)` → 帧循环 8 帧（每帧 `BeginFrame(0, 16666, frame)` → `CreateFence()` → `DestroyFence(fh)` → `Flush()`，背压自动限速）→ 调 `cleanup(enginePtr)` → `Terminate()`；`App` 对象 SHALL 在 `Run` 返回后销毁。

#### Scenario: 帧循环

- **WHEN** `App::Instance().Run(setup, cleanup)`
- **THEN** 8 帧命令全部被执行线程消费，`setup` 与 `cleanup` 各调用恰好一次，`Terminate()` 后进程干净退出

### Requirement: 闭环可观察性

在无 gtest 前提下，闭环执行结果 SHALL 可观察：`QueueCommand` 的探针 lambda SHALL 在执行线程执行并输出日志（证明记录→传输→回放闭环）；`VulkanDriver::createFenceS()` SHALL 返回自增 id 的非空句柄（首个 id 为 0），使 RETURN 路径返回值可打印验证。

#### Scenario: 探针执行

- **WHEN** `setup` 中 `engine.QueueCommand([...]{ LOG_INFO(...); })` 后帧循环 `Flush()`
- **THEN** 探针 lambda 在执行线程打印日志，恰执行一次

#### Scenario: 句柄可观察

- **WHEN** 连续两次 `CreateFence()`
- **THEN** 返回句柄 id 分别为 0、1，且 `operator bool` 为真

### Requirement: main 入口

`tests/main.cpp` SHALL 定义 `main()`，调用 `App::Instance().Run(setupLambda, cleanupLambda)` 并返回 0；不依赖 gtest 或任何测试框架。

#### Scenario: 程序入口

- **WHEN** 运行 `BackendTests` 可执行文件
- **THEN** 依次输出 Vulkan 初始化日志、帧循环日志、探针日志，退出码为 0
