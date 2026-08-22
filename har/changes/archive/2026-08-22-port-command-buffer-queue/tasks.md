# Tasks: Port CommandBufferQueue from Filament

## 1. Command.h（命令基类与终止符）

- [x] 1.1 新建 `src/command/Command.h`：命名空间 `Backend`，`#pragma once`；`CommandBase`（`ExecuteFn` 类型别名 = `void (*)(Driver&, CommandBase*, intptr_t*)`、`kObjectAlignment = alignof(std::max_align_t)`、静态 `Align(size_t)`、`Execute(Driver&)` 方法、`m_execute` 私有成员），`NoopCommand`（`alignas(kObjectAlignment)`，`intptr_t m_next` 存相对偏移，静态 `Execute` 回写 `next`）；include `Backend/Driver.h`；去 license 头，注释按项目规范（只写意图，如偏移量使执行循环可尾调用优化）
- [x] 1.2 类型转换用 `static_cast`/`reinterpret_cast`，不用 C 风格（`intptr_t((char*)next - (char*)this)` → `static_cast<intptr_t>(reinterpret_cast<char*>(next) - reinterpret_cast<char*>(this))`）

## 2. CommandBufferQueue.h

- [x] 2.1 新建 `src/command/CommandBufferQueue.h`：命名空间 `Backend`，`#pragma once`；`Range{void* begin; void* end;}`；公有 API 全 PascalCase（`GetCircularBuffer` 双重载、`GetCapacity`、`GetHighWatermark`、`WaitForCommands`、`ReleaseBuffer`、`Flush`、`RequestExit`、`IsPaused`、`SetPaused`、`IsExitRequested`）；不继承 `Ref`；私有成员 `m_` 前缀，受锁保护成员标 `UTILS_GUARDED_BY(m_lock)`；`static constexpr uint32_t kExitRequested = 0x31415926`
- [x] 2.2 确认不含任何 `__EXCEPTIONS` 分支与异常成员（`std::exception_ptr`/原子标志），不 include `<exception>`/`<atomic>`
- [x] 2.3 include 按规范分组：`Backend/DriverDefine.h` → `Utils/buffer/CircularBuffer.h`、`Utils/Compiler.h`、`Utils/thread/lock/LockGuard.h`、`Utils/thread/lock/UniqueLock.h` → C++ 标准库（`<condition_variable>`/`<cstdint>`/`<mutex>`/`<vector>`）

## 3. CommandBufferQueue.cpp

- [x] 3.1 构造/析构：`m_requiredSize` 按 `utils::CircularBuffer::GetBlockSize()` 向上对齐、`m_circularBuffer(std::max(m_requiredSize, bufferSize))`、`m_freeSpace(m_circularBuffer.Size())`，`LOG_ASSERT(m_circularBuffer.Size() >= m_requiredSize)`；析构 `LOG_ASSERT(m_commandBuffersToExecute.empty())`
- [x] 3.2 `Flush()`：空缓冲短路；`new (circularBuffer.Allocate(sizeof(NoopCommand))) NoopCommand(nullptr)`（含 `<new>`）；`GetBuffer()` 返回 `{tail, head}` 映射为 `Range`；溢出 `LOG_CRITICAL`（保留 "used/overflow bytes" 诊断）；入队 + `notify_one`；可用空间不足时 debug 构建更新 `m_highWatermark` 并 `LOG_DEBUG` 阻塞诊断，暂停态 `LOG_CRITICAL` 防死锁，`m_condition.wait(lock)` 背压等待；不含 `__EXCEPTIONS` 头部块
- [x] 3.3 `WaitForCommands()`：`utils::UniqueLock` + `m_condition.wait` 循环（空或暂停且未退出），返回 `std::move(m_commandBuffersToExecute)`；删除 `UTILS_HAS_THREADING` 分支
- [x] 3.4 `ReleaseBuffer`/`RequestExit`/`IsPaused`/`SetPaused`/`IsExitRequested`：`utils::LockGuard`，语义与 Filament 一致（归还空间、置哨兵、唤醒条件变量）
- [x] 3.5 include 按规范：`"CommandBufferQueue.h"`、`"Command.h"` → `Utils/Log.h` → 标准库（`<algorithm>`/`<new>`/`<utility>`）

## 4. 构建验证

- [x] 4.1 配置并编译 Backend 目标：确认 `file(GLOB_RECURSE src/*.cpp)` 自动收集 `src/command/*.cpp`，无需修改 CMake；无编译错误与警告
- [x] 4.2 编写临时验证程序（不入库，可放 `tests/` 或 `bin/` 外临时位置）：单线程验证 Flush 空短路与提交-消费-归还循环；双线程验证背压阻塞、`RequestExit` 唤醒、`SetPaused` 挂起
- [x] 4.3 清理临时验证程序，确认 `git status` 仅包含规划制品与 `src/command/` 新增源文件（注意工作区另有 `tests/CircularBufferTest.cpp` 的未提交删除，属既有状态，不纳入本次变更）
