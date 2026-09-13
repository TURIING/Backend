# Capability: backend-async

## Purpose

后端异步底座：为 `VulkanPipelineCache` 的并行着色器编译与 `VulkanSync` / `VulkanFence` / `VulkanTimerQuery` 的完成回调提供承载。上游分工为「`CallbackHandler` 是 app 侧实现的纯接口，`CallbackManager` 与 `CompilerThreadPool` 是后端自带的有状态件」；本能力域沿用该分工。

上游 `VulkanDriver.cpp` 中 `CallbackHandler` 出现的调用点包括：`setFrameScheduledCallback` / `setFrameCompletedCallback` / `createVertexBufferAsyncR` / `createIndexBufferAsyncR` / `createBufferObjectAsyncR` / `createTextureAsyncR` / `createTextureViewSwizzleAsyncR` / `updateBufferObjectAsyncR` / `updateIndexBufferAsyncR` / `update3DImageAsyncR` / `setVertexBufferObjectAsyncR` / `getPlatformSync`，共 12 处签名。这些签名要求 `CallbackHandler` 类型存在，但**不要求本项目实现它的任何具体策略**。

## ADDED Requirements
### Requirement: CallbackHandler 纯接口

`include/Backend/CallbackHandler.h` SHALL 定义 `Backend::CallbackHandler` 抽象类：

- 嵌套类型 `using Callback = void (*)(void* userData)`
- 纯虚 `virtual void post(Callback callback, void* userData) = 0`
- 虚析构（`= default` 即可，接口不拥有资源）

该接口 SHALL 为**纯接口**：不持有状态、不定义任何具体分发策略。后端 SHALL 只持有 `CallbackHandler*` 裸指针并调用 `post`，**SHALL NOT 拥有其生命周期**——`VulkanSync::CallbackData` 持有 `CallbackHandler*` 但不负责释放，此约定必须在注释中写明，否则会出现 app 侧 handler 已析构而后端仍持悬垂指针。

#### Scenario: 后端只持指针

- **WHEN** 后端代码持有 `CallbackHandler*` 并在 callback 触发时调用 `post`
- **THEN** 不涉及任何 `delete` / 引用计数操作

#### Scenario: app 侧提供实现

- **WHEN** app 实现 `CallbackHandler` 并传入驱动方法
- **THEN** 后端经该指针调用 `post`，具体线程/时机由 app 实现决定

### Requirement: CallbackManager 回调登记与派发

`src/CallbackManager.{h,cpp}` SHALL 提供后端内部使用的回调管理器，接口与上游对齐：

- `Handle Get()`：取一个空闲槽位句柄
- `SetCallback(CallbackHandler* handler, CallbackHandler::Callback callback, void* user)`：为当前槽位登记回调
- `Put(Handle)`：标记槽位可复用
- `Post(Handle)` / 批量派发：对登记的 `(handler, callback, user)` 调用 `handler->post(callback, user)`

`CallbackManager` SHALL 在构造时接收 `Driver&`（上游构造签名为 `CallbackManager(VulkanDriver& driver)`），用于在派发时访问驱动侧状态。槽位 SHALL 复用，不在每次登记时分配。

#### Scenario: 登记与派发往返

- **WHEN** `Get()` 取槽位、`SetCallback()` 登记后触发派发
- **THEN** 对应 `handler->post(callback, user)` 被调用一次，参数与登记时一致

#### Scenario: 槽位复用

- **WHEN** `Put(handle)` 归还槽位后再次 `Get()`
- **THEN** 可复用已归还的槽位，不产生新的分配

### Requirement: CompilerThreadPool 并行编译线程池

`src/CompilerThreadPool.{h,cpp}` SHALL 提供固定线程数的工作池：

- `Init(ThreadCount, NameCallback, PriorityCallback)`：启动 N 个工作线程；两个回调用于设置线程名与优先级
- `Queue(CompilerPriorityQueue priority, Token token, Task task)`：按优先级入队任务
- `Terminate()`：停止并 join 全部线程

线程等待 SHALL 使用 `std::condition_variable`（上游 `utils::Condition` 的等价物），任务体 SHALL 使用 `std::function<void()>`（上游 `utils::Invocable` 的等价物）。线程启动时 SHALL 调用 `NS_BD::JobSystem::SetThreadName` / `SetThreadPriority`。

#### Scenario: 任务在高优先级前让位

- **WHEN** 低优先级任务已在队列中，随后入队高优先级任务
- **THEN** 高优先级任务先被取出执行

#### Scenario: Terminate 回收全部线程

- **WHEN** 调用 `Terminate()`
- **THEN** 全部工作线程退出并 join 完成，重复调用安全

### Requirement: JobSystem 最小空实现

`src/JobSystem.h` SHALL 提供 `Backend::JobSystem::SetThreadName(char const*)` 与 `Backend::JobSystem::SetThreadPriority(Priority)` 两个函数及 `Priority` 枚举（至少含 `DISPLAY`）。

这两个函数是上游 `VulkanPipelineCache.cpp` 第 86、88 行对 `utils::JobSystem` 的**全部**使用。本能力域 SHALL NOT 移植 Filament 的工作窃取调度器（`utils/JobSystem.h` + `JobSystem.cpp` 逾千行）。

macOS 平台 SHALL 在 `SetThreadName` 内调用 `pthread_setname_np` 使调试器可识别线程名；`SetThreadPriority` SHALL 为空实现。

#### Scenario: 调用无崩溃

- **WHEN** `CompilerThreadPool` 启动线程时调用两个函数
- **THEN** 编译链接通过、运行期无崩溃；macOS 下调试器可见线程名

#### Scenario: 不引入调度器

- **WHEN** 全项目检索 `JobSystem`
- **THEN** 只有 `src/JobSystem.h` 的定义与 `CompilerThreadPool` 的两个调用点，无工作窃取调度实现

### Requirement: DriverBase 与 ServiceThread

`src/DriverBase.h` SHALL 提供 `Backend::DriverBase`，作为 `Driver` 与具体驱动之间的中间层。它是本能力域的核心，**不是骨架**——`CallbackManager` 依赖它的 `ScheduleCallback` 实现。

**构造与析构**：构造接收 `DriverConfig const&`；SHALL 在构造时启动一个 ServiceThread（`std::thread`），析构时 SHALL `StopServiceThread()`（幂等，`join` 后线程退出）。析构 SHALL 断言 Purge 队列已空。

**ServiceThread 语义**：循环等待回调队列，取空后**在锁外**逐个调用 `handler->Post(user, callback)`。不得持队列锁调用 `Post`——`Post` 由 app 实现，可能阻塞。

**ScheduleCallback**：

- `ScheduleCallback(CallbackHandler*, void* user, CallbackHandler::Callback)`：handler 非空时入 ServiceThread 队列并唤醒；为空时入 Purge 队列，留给主线程的 `Purge()` 执行
- 模板重载 `ScheduleCallback(CallbackHandler*, T&& functor)`：把可调用体在 `CallbackData` 的 8 指针内联存储里就地构造，投递后执行并析构。SHALL 以 `static_assert(sizeof(T) <= sizeof(CallbackData::storage))` 约束；`CallbackData` 经 `Obtain` / `Release` 成对管理（当前实现为 `new` / `delete`，与上游一致）

**Purge**：`final`；持锁 swap 出 Purge 队列后**在锁外**逐个执行，避免回调重入时死锁。

**围栏等待**：SHALL 提供 `WaitForFence(predicate)` / `WaitForFence(predicate, until)` / `SignalFence(action)` / `SetUnrecoverableError()` / `HasUnrecoverableError()`。等待期间 SHALL 检查不可恢复错误标志，命中时提前返回 `FenceStatus::Error`。

**调试钩子**：SHALL 实现 `DebugCommandBegin` / `DebugCommandEnd`（`Driver` 的纯虚）。上游实现整体包在 `if constexpr (FILAMENT_DEBUG_COMMANDS > FILAMENT_DEBUG_COMMANDS_NONE)` 中，默认开关下编译为空；项目未移植该体系，故以空实现等价落地，并在实现处注明。

**明确不移植**：

| 上游成员 | 理由 |
|---|---|
| `scheduleDestroy(BufferDescriptor&&)` / `scheduleDestroySlow` | 依赖 `BufferDescriptor`，属变更 2；变更 2 落地时补入 |
| `scheduleRelease(AcquiredImage const&)` | `AcquiredImage` 已按决策砍掉（外部图像） |
| `debugCommandBegin` 的 systrace / `DLOG` 分支 | 项目无 systrace 体系 |

#### Scenario: 构造启动、析构回收

- **WHEN** 创建再销毁一个 `DriverBase` 派生对象
- **THEN** ServiceThread 在构造时被启动、在析构时被 join，无泄漏或挂起

#### Scenario: handler 非空时经 ServiceThread 派发

- **WHEN** 以非空 handler 调用 `ScheduleCallback`
- **THEN** 回调入 ServiceThread 队列，由该线程调用 `handler->Post(user, callback)`

#### Scenario: handler 为空时留给 Purge

- **WHEN** 以 `nullptr` handler 调用 `ScheduleCallback`
- **THEN** 回调入 Purge 队列，`Purge()` 被调用时在主线程执行

#### Scenario: 锁外执行

- **WHEN** 检查 ServiceThread 循环与 `Purge()` 的实现
- **THEN** `handler->Post(...)` 与 Purge 回调均在释放队列锁之后调用

#### Scenario: 不可恢复错误中断等待

- **WHEN** 某线程阻塞在 `WaitForFence` 期间另一线程调用 `SetUnrecoverableError()`
- **THEN** 等待者被唤醒并返回 `FenceStatus::Error`

#### Scenario: 调试钩子无副作用

- **WHEN** 调用 `DebugCommandBegin` / `DebugCommandEnd`
- **THEN** 无任何输出与状态变更（与上游默认编译开关下的行为等价）

#### Scenario: 回调体过大编译失败

- **WHEN** 以 `sizeof(T) > 8 * sizeof(void*)` 的可调用体调用模板 `ScheduleCallback`
- **THEN** `static_assert` 触发编译失败

### Requirement: 适配约束
- `CallbackHandler.h` SHALL 位于 `include/Backend/`（公共头，app 侧需实现）
- `CallbackManager` / `CompilerThreadPool` / `DriverBase` / `JobSystem.h` SHALL 位于 `src/`（后端内部件）
- 命名 SHALL 遵循项目规范：类型 `PascalCase`、公有函数 `PascalCase`、私有函数 `camelCase`、成员 `m_camelCase`、常量 `kPascalCase`
- 类内声明 SHALL 遵循 `.dsh/rules/cpp.md` 的空行分组规则：访问修饰符段之间、特殊成员函数组与普通成员之间、函数声明区与成员变量区之间留空行
- `CallbackHandler*` 的所有权语义 SHALL 以注释写明（非平凡的生命周期约束）
- 注释 SHALL 遵循 `.dsh/rules/code-style.md`：只解释「为什么/意图」，不复述代码；单函数体内不超过 3 条
- 本能力域 SHALL NOT 引入 `JobQueue` / `WorkStealingJobQueue` / 工作窃取调度器
- 本能力域 SHALL NOT 引入 `FeatureFlagManager`（`VulkanDriver` 构造期的最小需求留待变更 7 处理）

#### Scenario: 编译通过

- **WHEN** 构建整个 `Backend` 静态库
- **THEN** 本能力域的全部源文件编译通过，不依赖 Filament 任何头文件

#### Scenario: 命名与风格合规

- **WHEN** 检查本能力域新增的类型与函数
- **THEN** 命名遵循项目规范（`PascalCase` 类型与公有方法、`m_camelCase` 私有成员、`kPascalCase` 常量），头文件使用 `#pragma once`，不保留上游 license / 文件头注释
