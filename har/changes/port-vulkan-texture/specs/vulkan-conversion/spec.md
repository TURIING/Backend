# Capability: vulkan-conversion

## Purpose

Vulkan 类型转换与图像工具层：把后端的平台无关类型（`TextureFormat` / `SamplerParams` / `CullMode` / `VulkanLayout`）转换为 Vulkan 原生类型，并提供图像布局转换、SPIR-V 校验与固定容量向量。上游对应物为 `backend/src/vulkan/utils/` 下的六个文件（约 1870 行）。

这是上游各组件引用密度最高的一层：`VulkanPipelineCache`（`GetCullMode` / `GetFrontFace` / `GetBlendFactor` / `GetPrimitiveTopology` / `GetStencilOp`）、`VulkanFboCache`（`GetVkFormat` / `IsVkDepthFormat`）、`VulkanTexture`（布局与 aspect）、`VulkanBlitter`（`TransitionLayout`）全部依赖它。

## ADDED Requirements
### Requirement: 命名空间统一为 VK_UTILS

本能力域的六个文件 SHALL 使用 `VK_UTILS` 命名空间，与既有 `src/vulkan/VkUtils.h` 合并为同一个 Vulkan 工具命名空间。

上游的 `filament::backend::fvkutils` SHALL NOT 引入。既有 `VkUtils.h` 中的 `Enumerate` / `IsVkDepthFormat` / `CheckVkResult` 与本能力域的符号 SHALL 共存于 `VK_UTILS`。

**重复定义核对**：既有 `VkUtils.h` 已有 `IsVkDepthFormat`，上游 `Conversion.h` 亦有同名函数。SHALL 合并为一份实现（保留既有实现，删除上游重复定义），并统一命名风格。

#### Scenario: 命名空间合并无冲突

- **WHEN** 同一翻译单元内同时使用 `VK_UTILS::Enumerate(...)` 与 `VK_UTILS::GetVkFormat(...)`
- **THEN** 两者均可解析，无重复定义错误

#### Scenario: IsVkDepthFormat 单一实现

- **WHEN** 全项目检索 `IsVkDepthFormat`
- **THEN** 只有一处定义，其余均为调用

### Requirement: Definitions 格式列表与位掩码

`src/vulkan/utils/Definitions.h` SHALL 提供：

- `using VkFormatList = std::vector<VkFormat>;`（上游 `utils::FixedCapacityVector` → `std::vector`）
- `constexpr VkFormat kAllVkFormats[]`：从上游 `ALL_VK_FORMATS` 机械复制，约 250 项。SHALL 配套编译期长度断言
- `DescriptorSetMask`：基于 `NS_UTILS::Bitset32`，提供 `operator[]`、`Set` / `Unset`、`ForEachSetBit`
- `UniformBufferBitmask` / `SamplerBitmask`：同上

`DescriptorSetMask` 的接口形态是硬约束——`VulkanDescriptorSetCache::commit` 用 `ForEachSetBit` 遍历，`VulkanDriver::bindPipeline` 用 `descriptorSetMaskTable` 构造。SHALL 逐方法对齐上游。

#### Scenario: 位掩码遍历

- **WHEN** 构造 `DescriptorSetMask` 并置位下标 0 与 2 后调用 `ForEachSetBit`
- **THEN** 回调按 0、2 顺序被调用两次

#### Scenario: 格式数组长度正确

- **WHEN** 编译含 `static_assert` 的 `Definitions.h`
- **THEN** 数组长度断言通过

### Requirement: Conversion 格式与枚举映射

`src/vulkan/utils/Conversion.{h,cpp}` SHALL 提供以下映射函数，逐值对齐上游：

| 函数 | 输入 → 输出 |
|---|---|
| `GetVkFormat` | `TextureFormat` → `VkFormat`（约 90 分支） |
| `IsVkDepthFormat` / `IsVkStencilFormat` | `VkFormat` → `bool`（与既有 `VkUtils.h` 合并） |
| `GetBlendFactor` | `BlendFunction` → `VkBlendFactor` |
| `GetBlendEquation` | `BlendEquation` → `VkBlendOp` |
| `GetCompareOp` | `SamplerCompareFunc` → `VkCompareOp` |
| `GetStencilOp` | `StencilOperation` → `VkStencilOp` |
| `GetWrapMode` | `SamplerWrapMode` → `VkSamplerAddressMode` |
| `GetFilter` | `SamplerMinFilter` / `SamplerMagFilter` → `VkFilter` |
| `GetMipmapMode` | `SamplerMinFilter` → `VkSamplerMipmapMode` |
| `GetCullMode` | `CullingMode` → `VkCullModeFlags` |
| `GetFrontFace` | `bool` → `VkFrontFace` |
| `GetPrimitiveTopology` | `PrimitiveType` → `VkPrimitiveTopology` |
| `GetSwizzleFilament` | `VkComponentSwizzle` + 索引 → `TextureSwizzle` |
| `GetImageAspect` | `VkFormat` → `VkImageAspectFlags` |
| `TransitionLayout` | 布局转换命令录制 |

全部单值映射 SHALL 使用 `CASE_FROM_TO(X, Y)` 宏（`.dsh/rules/cpp.md` 规定禁止逐 case 手写两行）。

`GetVkFormat` SHALL 为 `TextureFormat` 的每一个枚举值提供分支，不留 `default` 兜底到 `VK_FORMAT_UNDEFINED`——兜底会静默掩盖遗漏。若上游确有 `default`，SHALL 保留但以 `LOG_ASSERT(false)` 标记不可达。

#### Scenario: 枚举穷举无遗漏

- **WHEN** 以 `-Wswitch` 编译 `Conversion.cpp`
- **THEN** 无「枚举值未在 switch 中处理」告警

#### Scenario: 映射语义正确

- **WHEN** 调用 `GetVkFormat(TextureFormat::R8G8B8A8_UNORM)`
- **THEN** 返回 `VK_FORMAT_R8G8B8A8_UNORM`

#### Scenario: 条件返回值用宏压缩

- **WHEN** 某映射在整数/归一化两条分支间选择（如 `ElementType::BYTE` → `integer ? VK_FORMAT_R8_SINT : VK_FORMAT_R8_SSCALED`）
- **THEN** 以 `CASE_FROM_TO(ElementType::BYTE, integer ? VK_FORMAT_R8_SINT : VK_FORMAT_R8_SSCALED)` 单行表达

### Requirement: Image 布局与图像辅助

`src/vulkan/utils/Image.{h,cpp}` SHALL 提供：

- `enum class VulkanLayout`：`UNDEFINED` / `COLOR_ATTACHMENT` / `DEPTH_STENCIL_ATTACHMENT` / `DEPTH_SAMPLER` / `SHADER_READABLE` / `SHADER_WRITABLE` / `TRANSFER_SRC` / `TRANSFER_DST` / `PRESENT` 等（逐值对齐上游）
- `TransitionLayout(VkCommandBuffer, VkImage, VulkanLayout oldLayout, VulkanLayout newLayout, ...)`：录制图像布局转换
- `ReduceSampleCount(uint8_t requested)`：把请求的采样数降到设备支持值
- `IsVkFormatDepthOrStencil(VkFormat)` 等辅助

`VulkanLayout` 的枚举值 SHALL 与上游一致——它被 `VulkanTexture` 用于布局跟踪、被 `VulkanRenderTarget` 用于 `emitBarriersBeginRenderPass`。

#### Scenario: 布局转换可录制

- **WHEN** 在录制中的命令缓冲上调用 `TransitionLayout(cmd, image, VulkanLayout::UNDEFINED, VulkanLayout::COLOR_ATTACHMENT, ...)`
- **THEN** 录制一条 `vkCmdPipelineBarrier`，无校验层报错

#### Scenario: 采样数降级

- **WHEN** 请求 8 倍采样但设备仅支持到 4
- **THEN** `ReduceSampleCount(8)` 返回 4（或上游定义的等效降级值）

### Requirement: Spirv 校验

`src/vulkan/utils/Spirv.{h,cpp}` SHALL 提供 SPIR-V 二进制的基本校验（魔数、版本头、字数对齐）与 `VkShaderModule` 创建辅助。

**范围限定**：SHALL NOT 提供 GLSL → SPIR-V 编译能力，SHALL NOT 引入 `glslang` / `spirv-tools` 依赖。着色器二进制由前端产出。

#### Scenario: 合法 SPIR-V 通过校验

- **WHEN** 传入以 `0x07230203` 开头、长度对齐的二进制
- **THEN** 校验通过

#### Scenario: 非法二进制被拒绝

- **WHEN** 传入魔数错误的二进制
- **THEN** 校验失败并给出诊断，不创建 `VkShaderModule`

### Requirement: StaticVector 固定容量向量

`src/vulkan/utils/StaticVector.h` SHALL 提供 `VK_UTILS::StaticVector<T, N>`：栈上固定容量向量，提供 `PushBack` / `PopBack` / `Size` / `Empty` / `Clear` / `operator[]` / 迭代器。

容量溢出 SHALL 在 debug 构建下 `LOG_ASSERT` 中止。

**不得用 `std::vector` 替代**：该类型被用作命令流命令体成员（`VulkanProgram::BindingList`），要求无堆分配。既有 `VulkanCommandBuffer` 的 `std::array<T, 2>` 用法 SHALL 保持不动（两套并存，边界为「既有不动、新增用新」）。

#### Scenario: 栈上无分配

- **WHEN** 构造 `StaticVector<uint16_t, 16>` 并 `PushBack` 若干元素
- **THEN** 期间无堆分配（可用 `operator new` 计数器验证）

#### Scenario: 溢出断言

- **WHEN** debug 构建下向容量 4 的 `StaticVector` 压入第 5 个元素
- **THEN** `LOG_ASSERT` 触发中止

### Requirement: Helper 头文件级工具

`src/vulkan/utils/Helper.h` SHALL 提供上游头文件级小工具（`Hash` 特化、结构体判等辅助、`operator==` 组合）。逐符号对齐上游，不额外扩充。

#### Scenario: 哈希特化可用

- **WHEN** 以 `NS_UTILS::Hash<VkFormat>` 或上游等价的哈希特化作为 `unordered_map` 的哈希参数
- **THEN** 编译通过

### Requirement: 适配约束
- 六个文件 SHALL 位于 `src/vulkan/utils/`，命名空间为 `VK_UTILS`
- 上游文件头注释与 license 注释 SHALL 删除（`.dsh/rules/code-style.md`）
- `using namespace bluevk;` SHALL 删除——本项目用 `volk`
- 命名 SHALL 遵循项目规范：类型 `PascalCase`、函数 `PascalCase`、常量 `kPascalCase`、枚举值 `PascalCase`
  - **例外 1**：`VulkanLayout` 的枚举值 SHALL 保留上游 `UPPER_SNAKE_CASE`（如 `COLOR_ATTACHMENT`），理由与 `TextureFormat` 相同——逐值对照上游时机械性优先
  - **例外 2**：`VkFormat` / `VulkanLayout` 等承载 Vulkan 语义的名称保留上游拼写
- `utils::` 前缀符号 SHALL 按 `backend-utils` 的映射约定替换（`FixedCapacityVector` → `std::vector`、`bitset32` → `NS_UTILS::Bitset32`）
- 注释 SHALL 遵循 `.dsh/rules/code-style.md`：不复述映射关系（`CASE_FROM_TO` 已自明），只对非自明的映射（上游为兼容性做的特殊处理）说明意图
- 本能力域 SHALL NOT 引入 `utils::io::ostream` 依赖

#### Scenario: 编译通过

- **WHEN** 构建整个 `Backend` 静态库
- **THEN** 本能力域的全部源文件编译通过，不依赖 Filament 任何头文件

#### Scenario: 命名与风格合规

- **WHEN** 检查本能力域新增的类型与函数
- **THEN** 命名遵循项目规范（`PascalCase` 类型与公有方法、`m_camelCase` 私有成员、`kPascalCase` 常量），头文件使用 `#pragma once`，不保留上游 license / 文件头注释
