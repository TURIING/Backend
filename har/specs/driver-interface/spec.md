# Capability: driver-interface

## Purpose

Driver 抽象接口与 `DriverAPI.inc` 种子方法清单：`Driver` 从空壳升级为"异步方法非虚、同步方法纯虚"的接口骨架，`DriverAPI.inc` 追加 8 条真实方法声明（原版 179 条的前 8 条），为 `Dispatcher`/`CommandStream` 提供编译期类型基础。

## Requirements

### Requirement: Driver 抽象接口

`Driver` SHALL 保持继承 `NS_UTILS::Ref`（`SharedPtr` 的 `is_base_of_v` 静态断言要求）并保留 `DECLARE_SHARE_PTR_CLASS(Driver)`（`DriverPtr`）；SHALL 提供虚析构（`= default`）、纯虚 `Dispatcher GetDispatcher() const noexcept`（仅构造 `CommandStream` 时调用一次）、虚 `void Execute(std::function<void()> const&)`（默认实现直接调用 `fn()`，供驱动包装命令批执行）。

#### Scenario: 空壳到接口

- **WHEN** 声明 `Driver` 子类
- **THEN** 子类必须实现 `GetDispatcher()` 与全部同步方法（纯虚），否则编译失败

#### Scenario: Execute 默认钩子

- **WHEN** 子类未覆盖 `Execute()`
- **THEN** 调用时直接执行传入的 `fn()`，行为等价于无包装

### Requirement: 方法声明宏展开

`Driver` 内 SHALL 通过 `DriverAPI.inc` 展开方法声明：异步方法（`DECL_DRIVER_API`）展开为非虚内联空实现；异步带返回值（`DECL_DRIVER_API_RETURN`）展开为纯虚 `methodName##S()`（同步获取结果）+ 非虚 `methodName##R(RetType, params)` 空实现；同步方法（`DECL_DRIVER_API_SYNCHRONOUS`）展开为纯虚 `methodName(params)`。

#### Scenario: 种子集声明完整

- **WHEN** 编译包含 `Driver.h` 的翻译单元
- **THEN** `tick`/`beginFrame`/`flush`/`finish`/`resetState` 为非虚方法，`createFenceS` 为纯虚、`createFenceR` 非虚，`terminate` 为纯虚，全部编译通过

### Requirement: 种子方法清单

`DriverAPI.inc` 末尾 SHALL 追加种子方法清单，覆盖三条宏路径：`DECL_DRIVER_API_0(tick)`、`DECL_DRIVER_API_N(beginFrame, int64_t, monotonic_clock_ns, int64_t, refreshIntervalNs, uint32_t, frameId)`、`DECL_DRIVER_API_0(flush)`、`DECL_DRIVER_API_0(finish)`、`DECL_DRIVER_API_0(resetState)`、`DECL_DRIVER_API_TAGGED_R_N(FenceHandle, createFence)`、`DECL_DRIVER_API_N(destroyFence, FenceHandle, fh)`、`DECL_DRIVER_API_SYNCHRONOUS_0(void, terminate)`。方法名 SHALL 保持 Filament 原版拼写（与 `Driver` 声明严格一致，是 `COMMAND_TYPE` 机制的前提）；类型名 SHALL 不加 `backend::` 前缀（本地命名空间为 `Backend`，inc 在命名空间内展开）。

#### Scenario: 覆盖三条宏路径

- **WHEN** 统计种子集
- **THEN** `tick`/`flush`/`finish`/`resetState` 覆盖 0 参异步路径，`beginFrame` 覆盖多参异步路径，`createFence` 覆盖 RETURN 路径，`terminate` 覆盖同步路径

### Requirement: TAGGED 宏类名修正

`DriverAPI.inc` 的 `DECL_DRIVER_API_TAGGED_R_N` 与 `DECL_DRIVER_API_SYNCHRONOUS_TAGGED_N` 宏中的 `utils::ImmutableCString` SHALL 修正为 `utils::ImmutableString`（本地 Utils 类名），否则 `createFence` 展开后编译失败。

#### Scenario: TAGGED 宏可展开

- **WHEN** `createFence` 的 TAGGED 宏展开
- **THEN** 引用 `utils::ImmutableString`，`createFenceR` 携带 `ImmutableString&&` tag 参数，编译通过

### Requirement: VulkanDriver 适配

`VulkanDriver` SHALL 实现 `Dispatcher GetDispatcher() const noexcept override`（返回 `ConcreteDispatcher<VulkanDriver>::Make()`）与 `void terminate() override`（空桩），保持现有 `Create()` 形态不变。

#### Scenario: VulkanDriver 可编译

- **WHEN** 构建整个 `Backend` 静态库
- **THEN** `src/vulkan/VulkanDriver.cpp` 编译通过，`Create()` 返回 `DriverPtr` 行为不变

### Requirement: 无异常传播路径

`Driver` 接口与 `DriverAPI.inc` SHALL 不引入任何异常相关 API 与成员，延续 `-fno-exceptions` 兼容先例。

#### Scenario: 无异常构建

- **WHEN** 以禁用异常的配置编译
- **THEN** 编译通过，无 `__EXCEPTIONS` 分支
