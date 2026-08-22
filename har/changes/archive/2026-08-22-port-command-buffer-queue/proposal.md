# Port CommandBufferQueue from Filament

## Why

`port-circular-buffer` 已把 `CircularBuffer`（无锁单生产者环形缓冲）移植进 `Utils`，但消费端框架（生产者-消费者命令队列）尚未落地，design.md 中明确将其列为"后续独立工作"。`CommandBufferQueue` 是命令流框架的核心：记录线程 `Flush()` 提交命令块并实现背压，执行线程 `WaitForCommands()`/`ReleaseBuffer()` 消费并归还空间。移植后命令流的生产/消费骨架即可运转。

## What Changes

- 从 Filament 移植 `CommandBufferQueue`（`.h`/`.cpp`）到 `src/command/`，命名空间 `Backend`
- 新建 `src/command/Command.h`，仅含 `CommandBase` + `NoopCommand`（终止符），布局忠实原版，供后续 `CommandStream` 移植复用
- 不带 `__EXCEPTIONS` 分支：异常传播 API 与成员、`#else` 空桩全部删除
- 代码按项目规范重写：公有函数 PascalCase、成员 `m_` 前缀、常量 `kPascalCase`、去 license 头、注释重写
- 不继承 `NS_UTILS::Ref`，回归 Filament 原版类形状
- BREAKING 无（新组件，不影响既有代码）

## Capabilities

### New Capabilities

- `command-buffer-queue`: 命令缓冲队列能力（flush 提交 / waitForCommands 消费 / releaseBuffer 归还的背压控制模型）

### Modified Capabilities

无

## Impact

- 新增文件（`src/command/` 下，根 CMake `file(GLOB_RECURSE src/*.cpp)` 自动收集，无需改构建配置）：
  - `src/command/Command.h`
  - `src/command/CommandBufferQueue.h`
  - `src/command/CommandBufferQueue.cpp`
- 依赖：`Utils/buffer/CircularBuffer.h`（PascalCase API）、`Utils/thread/lock/LockGuard.h`/`UniqueLock.h`、`Utils/Log.h`（`LOG_ASSERT`/`LOG_DEBUG`/`LOG_CRITICAL`）、`Utils/Compiler.h`（`UTILS_GUARDED_BY`）、`Backend/Driver.h`（`NoopCommand::Execute` 签名引用）
- 不改动既有代码与构建配置
