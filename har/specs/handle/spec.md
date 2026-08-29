# Capability: handle

## Purpose

类型安全资源句柄：`HandleBase` 持有 `uint32_t` 句柄 id，`Handle<T>` 模板按资源类型区分；句柄可平凡复制、比较、类型安全转换，用于 Driver API 的返回值与销毁参数（`createFence` 等 RETURN 路径的前提）。

## Requirements

### Requirement: 句柄基类

`HandleBase` SHALL 持有 `HandleId`（`uint32_t`）私有成员，默认构造为 `nullid`（`UINT32_MAX`）；SHALL 提供 `operator bool`（`object != nullid`）、`clear()`（重置为 `nullid`）、`GetId()`；SHALL 提供显式 `HandleBase(HandleId)` 构造（内部使用），构造时 SHALL 断言 id 非 `nullid`（`LOG_ASSERT`，debug 构建拦截未初始化句柄）。

#### Scenario: 默认构造为空句柄

- **WHEN** 默认构造 `HandleBase`
- **THEN** `operator bool` 为 false，`GetId()` 返回 `nullid`

#### Scenario: 非法 id 构造断言

- **WHEN** 以 `nullid` 构造 `HandleBase`
- **THEN** debug 构建触发 `LOG_ASSERT`；release 构建行为未定义

### Requirement: 句柄拷贝与移动语义

`HandleBase` 的拷贝构造/赋值 SHALL 保留原句柄；移动构造/赋值 SHALL 转移 id 并将源置为 `nullid`。拷贝/移动接口 SHALL 为受保护（仅派生类可见）。

#### Scenario: 移动置空源

- **WHEN** 移动构造 `Handle<T> b = std::move(a)`
- **THEN** `b` 持有原 id，`a` 变为空句柄

### Requirement: 类型安全句柄模板

`Handle<T>` SHALL 继承 `HandleBase`，提供默认/拷贝/移动构造与显式拷贝/移动赋值（显式调用基类对应版本）；SHALL 提供显式 `Handle(HandleId)` 构造与完整的比较运算符（`==`/`!=`/`<`/`<=`/`>`/`>=`，按 `GetId()` 比较）；SHALL 提供类型安全转换构造 `Handle(Handle<B>)`（`std::is_base_of_v<T, B>` 约束，隐式）。

#### Scenario: 比较与转换

- **WHEN** 比较两个不同 id 的 `Handle<T>`，或将 `Handle<Derived>` 隐式转换为 `Handle<Base>`
- **THEN** 比较结果与 id 大小一致；转换成功且不丢失 id

### Requirement: 资源句柄别名

SHALL 前置声明 `Hw*` 资源结构体，并以 `using` 定义 `BufferObjectHandle`/`FenceHandle`/`IndexBufferHandle`/`ProgramHandle`/`RenderPrimitiveHandle`/`RenderTargetHandle`/`StreamHandle`/`SwapChainHandle`/`SyncHandle`/`TextureHandle`/`TimerQueryHandle`/`VertexBufferHandle`/`VertexBufferInfoHandle`/`DescriptorSetLayoutHandle`/`DescriptorSetHandle`/`MemoryMappedBufferHandle` 别名（`Handle<T>` 实例化）。

#### Scenario: 别名可用

- **WHEN** 使用 `FenceHandle`/`TextureHandle` 等别名声明变量
- **THEN** 各别名指向对应资源类型的 `Handle<T>` 特化，编译通过

### Requirement: 依赖映射

`assert_invariant` SHALL 映射为 `LOG_ASSERT`；Filament 版 `utils::io::ostream` 调试友元（NDEBUG 下的 `operator<<`）SHALL 不移植（本地无该类型）；头文件保护 SHALL 使用 `#pragma once`。

#### Scenario: 无 io::ostream 依赖

- **WHEN** 编译包含 `Handle.h` 的翻译单元
- **THEN** 不依赖任何 io/ostream 设施，仅依赖 `LOG_ASSERT` 与标准库
