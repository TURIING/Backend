# Capability: command-buffer-queue

## Purpose

生产者-消费者命令队列：记录线程 `Flush()` 提交命令块并实现背压，执行线程 `WaitForCommands()` 取走执行、`ReleaseBuffer()` 归还空间；支持挂起与退出控制，无异常传播路径。

## Requirements

### Requirement: 队列构造与容量对齐

队列构造 SHALL 将 `requiredSize` 向上对齐到 `CircularBuffer::GetBlockSize()`（页大小）的整数倍作为 `m_requiredSize`，环形缓冲大小 SHALL 取 `max(m_requiredSize, bufferSize)`；构造后 SHALL 断言缓冲实际大小不小于 `m_requiredSize`（`LOG_ASSERT`，debug 构建拦截）。

#### Scenario: 容量对齐与断言

- **WHEN** 以 `requiredSize=100, bufferSize=4096` 构造队列
- **THEN** `m_requiredSize` 为页大小整数倍且不超过缓冲大小，`GetCapacity()` 返回该对齐后的值，debug 构建下断言通过

### Requirement: Flush 提交命令块（生产者）

`Flush()` SHALL 在环形缓冲为空时直接返回（不写终止符、不取区间）；非空时 SHALL 先向缓冲追加 `NoopCommand` 终止符，再取出当前命令区间 `{begin, end}` 加入待执行队列并唤醒等待中的消费者，随后 SHALL 在可用空间低于 `m_requiredSize` 时阻塞等待消费者归还（背压）。

#### Scenario: 空缓冲短路

- **WHEN** 环形缓冲为空时调用 `Flush()`
- **THEN** 不分配终止符、不产生待执行区间，直接返回

#### Scenario: 提交与背压阻塞

- **WHEN** 缓冲非空且消费者未及时归还，可用空间低于 `m_requiredSize`
- **THEN** `Flush()` 阻塞直到 `ReleaseBuffer()` 归还足够空间后返回

#### Scenario: 溢出检测

- **WHEN** 单次命令块大小超过可用空间（缓冲配置过小，命令流已损坏）
- **THEN** `LOG_CRITICAL` 输出溢出字节数并中止进程

#### Scenario: 满且暂停防死锁

- **WHEN** 缓冲满且队列处于暂停态（`SetPaused(true)`），`Flush()` 将无法被消费推进
- **THEN** `LOG_CRITICAL` 输出死锁诊断并中止进程，而非无限等待

### Requirement: WaitForCommands 消费（消费者）

`WaitForCommands()` SHALL 在待执行队列为空、或队列暂停且未请求退出时阻塞等待；有可用命令时 SHALL 返回全部待执行区间并清空队列。

#### Scenario: 等待与取走

- **WHEN** 生产者 `Flush()` 提交了 N 个命令块
- **THEN** `WaitForCommands()` 返回包含这 N 个区间的向量，且队列被清空，随后可再次阻塞等待新提交

#### Scenario: 退出唤醒

- **WHEN** 消费者阻塞在 `WaitForCommands()` 时生产者调用 `RequestExit()`
- **THEN** 阻塞被唤醒，返回当前待执行区间（可能为空）

### Requirement: ReleaseBuffer 归还空间

`ReleaseBuffer()` SHALL 将区间大小加回可用空间并唤醒等待中的生产者；调用顺序 SHALL 与 `WaitForCommands()` 返回的区间顺序一致（消费者责任）。

#### Scenario: 归还唤醒生产者

- **WHEN** 消费者执行完毕调用 `ReleaseBuffer({begin, end})`
- **THEN** `m_freeSpace` 增加 `end - begin`，被背压阻塞的 `Flush()` 得以继续

### Requirement: 挂起与退出控制

`RequestExit()` SHALL 置退出哨兵（`kExitRequested`）并唤醒等待线程；`SetPaused(bool)` SHALL 切换挂起状态，取消挂起时唤醒等待线程；`IsPaused()`/`IsExitRequested()` SHALL 在锁内返回对应状态。

#### Scenario: 暂停抑制消费

- **WHEN** `SetPaused(true)` 后调用 `WaitForCommands()`
- **THEN** 即使有待执行命令也不返回，直到 `SetPaused(false)` 唤醒

### Requirement: 无异常传播路径

队列 SHALL 不提供任何异常相关 API 与成员：无 `hasUnrecoverableError`/`setUnrecoverableException`/`propagateBackendException`/`hasExceptionBeenRethrown`，无 `std::exception_ptr`/原子错误标志成员；源码 SHALL 不含 `__EXCEPTIONS` 分支。

#### Scenario: 无异常构建

- **WHEN** 以禁用异常（`-fno-exceptions`）的配置编译队列
- **THEN** 编译通过，无 `__EXCEPTIONS` 相关代码路径

### Requirement: 容量与水位查询

`GetCircularBuffer()` SHALL 返回底层环形缓冲引用（常量/非常量重载）；`GetCapacity()` SHALL 返回对齐后的 `m_requiredSize`；`GetHighWatermark()` SHALL 持锁返回历史最高占用量（仅 debug 构建统计更新，release 下恒为 0）。

#### Scenario: 查询接口

- **WHEN** 队列构建后查询 `GetCircularBuffer()`/`GetCapacity()`
- **THEN** 返回底层缓冲引用与对齐后容量，类型与语义与 Filament 原版一致
