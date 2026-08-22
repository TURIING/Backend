# Capability: dispatcher

## Purpose

函数指针派发表：`Dispatcher` 是仅含 `Execute` 函数指针的数据类（每个 Driver 方法一个成员），`ConcreteDispatcher<T>` 模板为具体驱动生成静态 trampoline 并填充表。`CommandStream` 构造时复制一份 `Dispatcher`，入队命令时把对应函数指针拷入 `CommandBase`，执行线程经指针间接调用具体驱动方法——实现"记录端零虚调用、执行端单次间接跳转"。

## Requirements

### Requirement: Dispatcher 纯数据表

`Dispatcher` SHALL 定义 `using Execute = void (*)(Driver&, CommandBase*, intptr_t*)`，并通过 `DriverAPI.inc` 展开为每个方法一个 `Execute methodName##_` 成员；SHALL 仅包含函数指针成员，无其他状态。`Driver` 与 `CommandBase` SHALL 前向声明即可（仅指针引用）。

#### Scenario: 表结构完整

- **WHEN** 实例化 `Dispatcher`
- **THEN** 对种子集中每个方法存在对应的 `methodName##_` 函数指针成员，默认值未初始化（由 `ConcreteDispatcher::Make()` 填充）

### Requirement: ConcreteDispatcher 派发生成

`ConcreteDispatcher<ConcreteDriver>` SHALL 提供静态 `Make()`：通过 `DriverAPI.inc` 展开，为每个异步方法生成静态 trampoline `(Driver&, CommandBase*, intptr_t*)`——`static_cast<ConcreteDriver&>(driver)` 后调用 `COMMAND_TYPE(methodName)::Execute(&ConcreteDriver::methodName, concreteDriver, base, next)`；RETURN 方法同理走 `methodName##R`；`Make()` SHALL 将每个 trampoline 地址填入 `Dispatcher` 对应成员并返回。

#### Scenario: 生成并填充

- **WHEN** 调用 `ConcreteDispatcher<VulkanDriver>::Make()`
- **THEN** 返回的 `Dispatcher` 中 `beginFrame_`/`createFence_`/`destroyFence_`/`tick_` 等均为有效函数指针，经其调用可到达 `VulkanDriver` 对应方法

### Requirement: 初始化时序

`Dispatcher` SHALL 在驱动初始化阶段一次性填充，且任何 `CommandStream` 调用发生之前完成（`GetDispatcher()` 在 `CommandStream` 构造时只调用一次）。

#### Scenario: 构造即定表

- **WHEN** 构造 `CommandStream` 且驱动 `GetDispatcher()` 已返回有效表
- **THEN** 表内容在整个生命周期内不变，后续入队命令直接复制指针，不再触碰驱动

### Requirement: 依赖映射

`CommandStreamDispatcher.h` 中的 `FILAMENT_TRACING_*`（SYSTRACE 宏）SHALL 不移植；trampoline 静态函数名 SHALL 保持与 Driver 方法同名（宏展开产物，遵循原版拼写）；头文件保护 SHALL 使用 `#pragma once`。

#### Scenario: 无 tracing 依赖

- **WHEN** 编译 `CommandStreamDispatcher.h`
- **THEN** 无 Perfetto/tracing 引用，仅依赖 `Driver.h`、`CommandStream.h` 与标准库头
