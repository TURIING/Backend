# Capability: backend-math

## Purpose

为上游 `src/vulkan/` 提供它所引用的 6 个 `math::` 类型的本地落脚点。项目无 math 库，而 `DriverEnums.h` 与 `VulkanDriver.cpp` 直接使用 `math::double4` / `math::float2` / `math::uint2` / `math::uint3` / `math::vec2` / `math::mat3f`——不补齐这些类型，`DriverAPI.inc` 中相关方法签名无法书写。

本能力域**只提供被实际引用的运算子集**，不追求与上游 Filament `libmath` 的 API 等价。

## ADDED Requirements
### Requirement: 命名空间与落位

math 类型 SHALL 定义在**全局命名空间 `math`**（非 `NS_UTILS::math`、非 `Backend::`），头文件位于 `3rd/Utils/include/Utils/math/`。

理由：上游调用点写作 `math::uint3 workGroupCount`、`math::double4 clearColor`；定义为全局 `math` 可使上游调用点**零改写**。定义为 `NS_UTILS::math` 会使每处调用点都需改写或引入命名空间别名，前者是噪音改动，后者污染全局命名空间。

头文件 SHALL 使用 `#pragma once`；SHALL NOT 引入除 C++ 标准库外的任何依赖。

#### Scenario: 上游调用点零改写

- **WHEN** 移植上游代码片段 `void VulkanDriver::dispatchCompute(Handle<HwProgram> program, math::uint3 workGroupCount)`
- **THEN** 编译通过，无需添加命名空间别名或改写类型名

#### Scenario: 头部自足

- **WHEN** 单独 include `Utils/math/Vector.h`
- **THEN** 编译通过，无未解析的外部符号

### Requirement: 向量类型

`Utils/math/Vector.h` SHALL 提供以下 POD 类型（分量类型与分量数）：

| 类型 | 分量 | 用途（上游引用点） |
|---|---|---|
| `math::float2` | 2 × float | `DriverAPI getClipSpaceParams()` 返回 |
| `math::float3` | 3 × float | 预留（上游 `RenderPassParams` 相关路径可能引用） |
| `math::float4` | 4 × float | 预留 |
| `math::uint2` | 2 × uint32_t | 上游 6 处引用 |
| `math::uint3` | 3 × uint32_t | `DriverAPI dispatchCompute(math::uint3)` |
| `math::vec2` | 2 × float | 上游 2 处引用 |
| `math::double4` | 4 × double | `ClearColorValue` 别名 |

每个类型 SHALL 提供：

- 默认构造（零初始化）
- 逐分量构造
- 聚合式 `operator[]` 或具名分量访问（`.x` / `.y` / `.z` / `.w`）
- 相等比较 `operator==` / `operator!=`

本能力域 SHALL NOT 提供矩阵乘法、四元数、几何求交、噪声等上游 `libmath` 的其余内容。

#### Scenario: 逐分量构造与访问

- **WHEN** 构造 `math::uint3 v{1, 2, 3}` 并访问 `v.x` / `v.y` / `v.z`
- **THEN** 分别得到 1 / 2 / 3

#### Scenario: 零初始化默认构造

- **WHEN** 默认构造 `math::double4 c{}`
- **THEN** 四个分量为 0.0

#### Scenario: ClearColorValue 别名可用

- **WHEN** 以 `ClearColorValue` 作为 `RenderPassParams::clearColor` 的类型
- **THEN** `using ClearColorValue = math::double4;` 解析成功，可逐分量赋值

### Requirement: 矩阵类型

`Utils/math/Matrix.h` SHALL 提供 `math::mat3f`：3×3 单精度浮点矩阵 POD，提供默认构造（单位矩阵）与逐元素构造。

该类型的唯一引用点是 `DriverAPI setAcquiredImage(..., math::mat3f const& transform, ...)`。用户决策为砍掉 `VulkanExternalImageManager` / `VulkanStreamedImageManager` 的**实现**，但方法**签名**须保留以维持 `DriverAPI.inc` 的完整性，因此 `mat3f` 必须存在。

本能力域 SHALL NOT 提供矩阵求逆、转置、乘法等运算，除非上游 Vulkan 路径实际调用。

#### Scenario: 默认构造为单位矩阵

- **WHEN** 默认构造 `math::mat3f m{}`
- **THEN** 对角线元素为 1、其余为 0

#### Scenario: setAcquiredImage 签名可写

- **WHEN** 在 `DriverAPI.inc` 书写 `DECL_DRIVER_API_SYNCHRONOUS_N(void, setAcquiredImage, ...)`
- **THEN** 参数列表中 `math::mat3f const&` 可解析，编译通过

### Requirement: 适配约束
- 类型 SHALL 为 POD（无虚函数、无用户定义析构、无堆分配），以保证可 memcpy、可作为命令流命令体成员被 placement-new 构造
- 命名 SHALL 遵循项目规范：类型 `PascalCase`（`Float2` 等与上游 `float2` 冲突时**保留上游小写名**，理由同命名空间决策——上游调用点零改写优先）
- 本能力域 SHALL NOT 使用 `Utils/Macro.h` 的 `NS_UTILS` 宏（类型不在 `utils` 命名空间内）
- 单元测试 SHALL 落在 `3rd/Utils/tests/`，覆盖各 Scenario

#### Scenario: 编译通过

- **WHEN** 构建整个 `Backend` 静态库
- **THEN** 本能力域的全部源文件编译通过，不依赖 Filament 任何头文件

#### Scenario: 命名与风格合规

- **WHEN** 检查本能力域新增的类型与函数
- **THEN** 命名遵循项目规范（`PascalCase` 类型与公有方法、`m_camelCase` 私有成员、`kPascalCase` 常量），头文件使用 `#pragma once`，不保留上游 license / 文件头注释
