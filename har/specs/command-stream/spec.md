# Capability: command-stream

## Purpose

命令流主体：`CommandType` 模板把 Driver 方法指针与参数包固化为命令对象（参数存 `std::tuple`），`CustomCommand` 包装 `std::function`，`CommandStream` 提供记录端 API（经 `DriverAPI.inc` 宏展开）与执行循环；复用已移植的 `CommandBase`/`NoopCommand`（`src/command/Command.h`）。

## Requirements

### Requirement: 模板命令类型

`CommandType<void (Driver::*)(ARGS...)>::Command<&Driver::method>` SHALL 以 `SavedParameters = std::tuple<std::remove_reference_t<ARGS>...>` 保存构造参数；SHALL 对齐到 `CommandBase::kObjectAlignment` 并 `static_assert` 对齐与大小约束；SHALL 提供静态 `Execute(M&& method, D&& driver, CommandBase*, intptr_t*)`：`*next = sizeof(Command)`，经 `apply()` 解包 tuple 调用方法，随后析构自身；SHALL 可移动、默认构造禁用，且提供返回原指针的 placement `operator new(size_t, void*)`。

#### Scenario: 参数打包与解包

- **WHEN** 以 `beginFrame(100, 16, 1)` 入队
- **THEN** 命令对象内 tuple 保存三个参数；执行时按序调用 `driver.beginFrame(100, 16, 1)`

#### Scenario: 命令链偏移

- **WHEN** 命令块内连续 N 个命令，`Execute` 依次返回 `sizeof(Command)`
- **THEN** 执行循环沿指针顺序遍历全部命令，末尾终止符结束

### Requirement: COMMAND_TYPE 宏

SHALL 定义 `COMMAND_TYPE(method)` 宏，将 `Driver::method` 映射为 `CommandType<decltype(&Driver::method)>::Command<&Driver::method>`，供宏展开与 `ConcreteDispatcher` 复用。

#### Scenario: 宏映射

- **WHEN** 以 `COMMAND_TYPE(beginFrame)` 实例化
- **THEN** 得到绑定 `&Driver::beginFrame` 的具体命令类型，可 placement new 与执行

### Requirement: CustomCommand

`CustomCommand` SHALL 继承 `CommandBase`，持有 `std::function<void()>`，对齐到 `CommandBase::kObjectAlignment`；静态 `Execute` SHALL 将 `*next` 置为 `sizeof(CustomCommand)`、调用 `m_command`、随后析构自身；SHALL 可移动。

#### Scenario: 队列命令执行

- **WHEN** 记录 `QueueCommand(lambda)` 并执行命令块
- **THEN** `lambda` 被执行恰好一次，命令内存被析构

### Requirement: CommandStream 记录端

`CommandStream` SHALL 继承 `NS_UTILS::Ref` 并声明 `DECLARE_SHARE_PTR_CLASS(CommandStream)`（`CommandStreamPtr`）；构造 `(DriverPtr driver, utils::CircularBuffer& buffer)` SHALL 复制 `driver->GetDispatcher()` 到 `m_dispatcher`，debug 构建记录 `m_threadId`（`std::this_thread::get_id()`）；SHALL 不可拷贝/移动；SHALL 提供 `GetCircularBuffer()`（常量/非常量重载）。经 `DriverAPI.inc` 展开的记录方法：异步方法 SHALL 计算 `sizeof(Cmd)` 在环形缓冲分配并 placement new 命令；RETURN 方法 SHALL 先同步调 `methodName##S()` 取结果、入队 `methodName##R` 命令、返回结果；同步方法 SHALL 直接 `apply(&Driver::methodName, *m_driver, ...)` 调用，不排队。宏中 `DEBUG_COMMAND_BEGIN/END`（依赖 `Driver::debugCommandBegin/End` 虚函数）SHALL 裁剪。

#### Scenario: 异步入队

- **WHEN** 调用 `BeginFrame(...)`/`Flush()`/`Tick()`
- **THEN** 命令写入缓冲，未执行前驱动对应方法不被调用

#### Scenario: RETURN 路径

- **WHEN** 调用 `CreateFence()`
- **THEN** 同步调用 `CreateFenceS()` 返回句柄，同时入队 `createFenceR` 命令；执行时驱动收到句柄与 tag

#### Scenario: 同步直调

- **WHEN** 调用 `Terminate()`
- **THEN** 立即调用 `Driver::terminate()`，缓冲无写入

### Requirement: 执行循环

`CommandStream::Execute(void*)` SHALL 将缓冲区间起点解释为 `CommandBase*`，在 `m_driver->Execute(fn)` 钩子内沿 `p = p->Execute(driver)` 遍历命令链至空指针。`QueueCommand(std::function<void()>)` SHALL 在缓冲中构造 `CustomCommand`。`Allocate(size, alignment)` SHALL 校验对齐为 2 的幂（`LOG_ASSERT`）、在命令块内插入 `NoopCommand` 标记辅助内存边界并返回对齐后的用户指针；`AllocatePod<T>(count, alignment)` SHALL 仅接受平凡析构类型并转发 `Allocate()`。

#### Scenario: 命令链执行

- **WHEN** 记录端写入命令块并 `Flush()`，执行端 `WaitForCommands()` 后调用 `Execute(range)`
- **THEN** 全部命令按记录顺序执行，命令对象析构，`ReleaseBuffer()` 后缓冲可复用

#### Scenario: NoopCommand 跳过辅助内存

- **WHEN** `Allocate()` 分配辅助内存且其后继续记录命令
- **THEN** 执行时辅助内存区被 `NoopCommand` 跳过，后续命令正常执行

### Requirement: 单线程写入断言

`allocateCommand`（私有）SHALL 在 debug 构建断言调用线程与 `debugThreading()` 记录的线程一致（`LOG_ASSERT(std::this_thread::get_id() == m_threadId)`）；`debugThreading()` SHALL 在 debug 构建记录当前线程 id。

#### Scenario: 记录线程断言

- **WHEN** debug 构建下记录线程先调用 `DebugThreading()` 再写入命令
- **THEN** 断言通过；若其他线程调用记录 API 则触发 `LOG_ASSERT`

### Requirement: 依赖映射

`assert_invariant` SHALL 映射为 `LOG_ASSERT`；`utils::ThreadUtils` SHALL 由 `std::this_thread::get_id()` 替代；`UTILS_RESTRICT` SHALL 不使用（`DriverPtr` 持有）；`FILAMENT_TRACING_*`/`Profiler`/`CallStack`/RTTI demangle（`DEBUG_COMMAND_STREAM` log）SHALL 不移植；`CommandBase`/`NoopCommand` SHALL 复用 `src/command/Command.h`，不重复定义；头文件保护 SHALL 使用 `#pragma once`。

#### Scenario: 依赖收敛

- **WHEN** 编译 `CommandStream.h`/`CommandStream.cpp`
- **THEN** 仅依赖已移植设施（`Utils::CircularBuffer`、`Command.h`、`Dispatcher.h`、`Driver.h`）与标准库

### Requirement: 无异常传播路径

`CommandStream` SHALL 不引入异常相关 API 与成员，延续 `-fno-exceptions` 兼容先例。

#### Scenario: 无异常构建

- **WHEN** 以禁用异常的配置编译
- **THEN** 编译通过，无 `__EXCEPTIONS` 分支
