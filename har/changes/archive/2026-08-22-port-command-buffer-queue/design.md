# Design: Port CommandBufferQueue from Filament

## Context

`port-circular-buffer` 已完成 `CircularBuffer` 移植（`Utils::CircularBuffer`，PascalCase API：`GetBlockSize`/`Allocate`/`GetBuffer`/`Empty`/`Size`，`GetBuffer()` 返回 `{tail, head}`），其 design.md 明确将 `CommandBufferQueue`/`CommandStream` 列为后续独立工作。项目历史上曾有一版本地移植（`src/CommandBufferQueue.{h,cpp}` + `src/CommandStream.h`，提交 `9a8c84e` 随本地 CircularBuffer 一并移除），git 历史可对照：其已按项目规范改过一轮（`m_` 前缀、`kExitRequested`、`LOG_ASSERT`/`LOG_CRITICAL` 映射），但保留着 `__EXCEPTIONS` 分支、camelCase 方法名、`Ref` 继承与旧 snake_case CircularBuffer API。

`src/command/` 空目录已建，根 CMake `file(GLOB_RECURSE src/*.cpp)` 自动收集，无需改构建。`Utils` 已有 `LockGuard`/`UniqueLock`（带线程注解）、`Log.h`（`LOG_ASSERT`/`LOG_DEBUG`/`LOG_CRITICAL`）、`Compiler.h`（`UTILS_GUARDED_BY`）。

## Goals / Non-Goals

**Goals:**
- 移植 `CommandBufferQueue`（`.h`/`.cpp`）到 `src/command/`，命名空间 `Backend`，生产/消费/背压语义与 Filament 一致
- 新建 `src/command/Command.h`，仅含 `CommandBase` + `NoopCommand`，布局与 Filament 逐字段一致，供后续 `CommandStream` 移植复用
- 完全剔除 `__EXCEPTIONS` 分支（异常 API、异常成员、`#else` 空桩）
- API 按项目规范：公有函数 PascalCase、成员 `m_` 前缀、常量 `kPascalCase`
- 不继承 `NS_UTILS::Ref`（回归 Filament 原版类形状）
- 依赖映射到现有 Utils 设施，不改动既有代码与构建配置

**Non-Goals:**
- 不移植 `CommandStream`/`CustomCommand`/命令执行循环（后续独立工作）
- 不移植 `Dispatcher`/Driver 方法派发机制
- 不引入 Perfetto tracing（`FILAMENT_TRACING_*` 直接删除）
- 不新增单元测试目标（与 `port-circular-buffer` 先例一致，验证用临时程序）

## Decisions

### D1: 文件布局

`src/command/Command.h`（`CommandBase` + `NoopCommand`）、`src/command/CommandBufferQueue.h`、`src/command/CommandBufferQueue.cpp`。`Command.h` 独立成文件而非塞进队列头：`NoopCommand` 是命令流公共设施，后续 `CommandStream` 移植直接复用，不越层。

### D2: `__EXCEPTIONS` 全删

用户决策：带 `__EXCEPTIONS` 分支的代码不迁移。头文件删除 `hasUnrecoverableError`/`setUnrecoverableException`/`propagateBackendException`/`hasExceptionBeenRethrown` 及 `#else` 空桩，删除 `m_backendException`/`m_hasUnrecoverableError`/`m_exceptionRethrown`；`.cpp` 删除 `propagateBackendException()` 与 `Flush()` 头部的异常检测块。连带 `<exception>`/`<atomic>` include 不再需要。原版 `UTILS_VERY_UNLIKELY` 的两处使用恰好都在异常块内，本项目 `Compiler.h` 无需补充该宏。

### D3: API 命名与成员

公有函数按项目规范 PascalCase：`GetCircularBuffer`/`GetCapacity`/`GetHighWatermark`/`WaitForCommands`/`ReleaseBuffer`/`Flush`/`RequestExit`/`IsPaused`/`SetPaused`/`IsExitRequested`；私有成员 `m_` 前缀（`m_requiredSize`/`m_circularBuffer`/`m_lock`/`m_condition`/`m_commandBuffersToExecute`/`m_freeSpace`/`m_highWatermark`/`m_exitRequested`/`m_paused`）；常量 `kExitRequested = 0x31415926`。与 `Utils::CircularBuffer` 移植口径一致。`Command.h` 内 `Execute` 类型别名与执行方法同名冲突，typedef 更名 `ExecuteFn`、方法 `Execute(Driver&)`。

### D4: 回归 Filament 原版类形状

不继承 `NS_UTILS::Ref`、无 `DECLARE_SHARE_PTR_CLASS`。Filament 原版即普通类，生命周期由拥有者管理；旧移植的 `Ref` 继承属项目自加，用户决策去掉。析构函数相应去掉 `override`。

### D5: 锁用 Utils 封装并补 `UTILS_GUARDED_BY`

`LockGuard`/`UniqueLock` 继承自 `std::lock_guard`/`std::unique_lock` 且带线程注解，`std::condition_variable::wait` 接受 `std::unique_lock` 基类子对象，交互无碍。受 `m_lock` 保护的成员按 Filament 原版补 `UTILS_GUARDED_BY(m_lock)`，配合带注解的锁使 Clang 线程安全检查生效（旧移植删掉了标注，本次补回）。

### D6: Filament 依赖映射

| Filament 依赖 | 本项目替代 |
|---|---|
| `CircularBuffer`（私有后端版） | `Utils::CircularBuffer`，API 换 PascalCase |
| `assert_invariant`（debug.h） | `LOG_ASSERT`（NDEBUG 下为空，语义一致） |
| `FILAMENT_CHECK_POSTCONDITION`（Panic.h） | `LOG_CRITICAL`（输出诊断并 abort），溢出/死锁两条消息保留语义 |
| `DLOG(INFO)`（Logger.h） | `LOG_DEBUG`（本项目仅 debug 构建定义，与 DLOG 语义一致） |
| `FILAMENT_TRACING_*`（Tracing.h，Perfetto） | 删除 |
| `UTILS_HAS_THREADING`（`waitForCommands` 的 `if constexpr`） | 删除该分支（桌面构建恒有线程） |
| `LockGuard`/`UniqueLock`（utils/Mutex.h） | `Utils/thread/lock/` 封装 |

### D7: `GetBuffer()` 返回区间适配

`Utils::CircularBuffer::GetBuffer()` 返回 `{tail, head}`（语义与 Filament 的 `{begin, end}` 相同：tail=begin、head=end），`Flush()` 中直接以 `Range{range.tail, range.head}` 入队，不引入命名混乱。

## Risks / Trade-offs

- [`NoopCommand` 与未来 `CommandStream` 移植的兼容] → `Command.h` 布局与 Filament 逐字段一致（`alignas(max_align_t)` + 函数指针 + `intptr_t` 相对偏移），后续直接复用
- [`LOG_ASSERT` 在 NDEBUG 下不评估表达式] → 与 `assert_invariant` 语义一致，调用处均为无副作用表达式
- [`LOG_CRITICAL` 替代 `FILAMENT_CHECK_POSTCONDITION` 丢失流式消息] → 消息内容保留并改为 fmt 参数化，行为等价（输出 + abort）
- [去除异常路径后后端错误处理无通道] → 用户明确决策；异常语义本就在 `__EXCEPTIONS` 分支内，后端错误处理方案留待后续独立设计

## Open Questions

无（探索阶段已与用户确认：`Command.h` 独立、PascalCase、去 `Ref` 继承、先建规划）
