# Capability: backend-enums

## Purpose

后端类型定义层：为 `DriverAPI.inc` 的 143 条方法声明提供全部参数与返回类型，为后续 5 个变更的 Vulkan 组件提供公共契约。上游对应物为 `backend/include/backend/DriverEnums.h`（约 1780 行、50 个类型）与 `backend/include/backend/TargetBufferInfo.h`。

本能力域是**纯类型层**：不引入任何实现、不追加任何 `DriverAPI.inc` 方法声明。

## ADDED Requirements
### Requirement: DriverDefine.h 单文件扩展

`include/Backend/DriverDefine.h` SHALL 保持为后端类型定义的**唯一入口**，按上游 `DriverEnums.h` 的分节顺序整体扩展。SHALL NOT 按「纹理 / 管线 / 渲染通道」拆分为多个头文件。

现有 9 个类型（`BackendType` / `StereoscopicType` / `GpuContextPriority` / `FenceStatus` / `BufferUsage` / `BufferObjectBinding` / `ElementType` / `DriverConfig` / `Attribute`）SHALL 保持类型名、枚举值、成员与语义不变（纯追加，非修改）。

`AttributeArray`（`std::array<Attribute, MAX_VERTEX_ATTRIBUTE_COUNT>`）SHALL 保持现状。

#### Scenario: 既有类型零变化

- **WHEN** 编译既有 `src/Driver.cpp`（`GetElementTypeSize` 的 26 分支 `switch`）与 `src/HwDefine.h`
- **THEN** 无需任何改动即编译通过

#### Scenario: 新类型可独立使用

- **WHEN** 声明 `void f(PipelineState const& ps, RenderPassParams const& params, TextureFormat fmt)`
- **THEN** 仅 include `Backend/DriverDefine.h` 即可编译通过

### Requirement: 渲染通道族类型

`DriverDefine.h` SHALL 提供：

- `TargetBufferFlags`：`enum class : uint32_t`，含 `NONE` / `COLOR` / `COLOR0..COLOR7` / `DEPTH` / `STENCIL` / `ALL`，并提供位运算（`|` / `&` / `~` / `|=` / `&=`）
- `RenderPassFlags`：`struct`，含 `clear` / `discardStart` / `discardEnd`（均为 `TargetBufferFlags`）
- `Viewport`：`struct`，含 `left` / `bottom` / `width` / `height`（`int32_t`）
- `DepthRange`：`struct`，含 `near` / `far`（`float`）
- `ClearColorValue`：`using ClearColorValue = math::double4;`
- `RenderPassParams`：`struct`，含 `flags` / `clearColor` / `clearDepth` / `clearStencil` / `viewport` / `depthRange` / `readOnlyDepthStencil` / `subpassMask` 等（逐字段对齐上游）

`TargetBufferFlags` 的位运算 SHALL 以 `constexpr` 具名函数或运算符实现，并 SHALL 提供 `HasAnyFlag(value, flags)` 形式的判定（与既有 `BufferUsage` 的处理方式一致）。`TargetBufferFlags::NONE` 恰为 0，故 SHALL NOT 提供 `operator bool`。

`RenderPassParams::READONLY_DEPTH` / `READONLY_STENCIL` SHALL 按上游作为静态常量提供。

#### Scenario: 位标志组合

- **WHEN** 计算 `TargetBufferFlags::COLOR | TargetBufferFlags::DEPTH`
- **THEN** 得到同时含两位置位的值；`HasAnyFlag(result, TargetBufferFlags::DEPTH)` 为真

#### Scenario: 默认构造零值

- **WHEN** 默认构造 `RenderPassParams{}`
- **THEN** `flags.clear` / `flags.discardStart` / `flags.discardEnd` 均为 `TargetBufferFlags::NONE`

### Requirement: 管线族类型

`DriverDefine.h` SHALL 提供：

- `PrimitiveType` / `CullingMode` / `BlendEquation` / `BlendFunction` / `StencilOperation` / `StencilFace`（`enum class`）
- `PolygonOffset`：`struct`，含 `constant` / `slope`（`float`）
- `RasterState`：`struct`，含 `culling` / `blendEquationRGB` / `blendEquationAlpha` / `blendFunctionSrcRGB` / `blendFunctionSrcAlpha` / `blendFunctionDstRGB` / `blendFunctionDstAlpha` / `depthWrite` / `colorWrite` / `alphaToCoverage` / `inverseFrontFaces` / `depthClamp` / `depthFunc`（`SamplerCompareFunc`），并提供 `HasBlending()` 查询
- `StencilState`：`struct`，含 `front` / `back` 两组 `StencilStateOps`（`comparison` / `depthFailOp` / `passOp` / `failOp` / `referenceValue` / `readMask` / `writeMask`）
- `PipelineState`：`struct`，含 `program` / `pipelineLayout`（`DescriptorSetLayout` 数组）/ `vertexBufferInfo` / `rasterState` / `stencilState` / `polygonOffset` / `primitiveType`

`PipelineState` / `RasterState` / `StencilState` / `DescriptorSetLayout` **四个结构的字段名、类型、顺序与默认值 SHALL 逐字段对齐上游**。理由：`VulkanDriver::bindPipelineImpl` 逐字段读取这些成员并转换为 `VulkanPipelineCache::RasterState`，任何偏差都会在变更 6/7 引发连锁改写；且 `bindPipeline` 内有 `mPipelineState = {}` 整体重置与基于 `setLayout` 数组形态的 `std::transform`，对成员布局有隐含依赖。

#### Scenario: PipelineState 逐字段可用

- **WHEN** 按上游 `bindPipelineImpl` 的读法访问 `pipelineState.pipelineLayout.setLayout[i]` / `.program` / `.vertexBufferInfo` / `.rasterState.culling` / `.stencilState.front`
- **THEN** 每个访问点编译通过且语义与上游一致

#### Scenario: 默认构造可整体重置

- **WHEN** 执行 `mPipelineState = {}`（上游 `bindPipeline` 首行语义）
- **THEN** 全部成员回到默认值，编译通过

### Requirement: 纹理与采样族类型

`DriverDefine.h` SHALL 提供：

- `TextureType`、`SamplerType`、`SamplerFormat`、`SubpassType`、`TextureCubemapFace`
- `TextureFormat`：`enum class : uint16_t`，约 90 个枚举值，**逐值与上游一致**（含 `_SNORM` / `_SSCALED` / `_USCALED` / `_SRGB` / `_INT` / `_UINT` 等后缀变体）
- `TextureUsage`：`enum class : uint16_t`，位标志形式（`DEFAULT` / `STATIC` / `DYNAMIC` / `BLIT_SRC` / `BLIT_DST` / `UPLOADABLE` / `SAMPLEABLE` / `COLOR_ATTACHMENT` / `DEPTH_ATTACHMENT` / `STENCIL_ATTACHMENT` / `SUBPASS_INPUT` / `PROTECTED`），提供位运算
- `TextureSwizzle`：`enum class : uint8_t`
- `SamplerWrapMode` / `SamplerMinFilter` / `SamplerMagFilter` / `SamplerCompareMode` / `SamplerCompareFunc`
- `SamplerParams`：`struct`，以上字段 + `anisotropy` + `padding`，并以 `unsigned` 位域打包（逐位域对齐上游）

`TextureFormat` 的枚举值 SHALL NOT 在本能力域提供任何到 `VkFormat` 的映射函数——映射属变更 4 的 `fvkutils::getVkFormat`（穷举 `switch`）。

`SamplerParams` 的位域布局 SHALL 逐位对齐上游——它被 `VulkanSamplerCache` 用作 `std::unordered_map` 的键的一部分，位域裁剪会影响哈希一致性。

#### Scenario: 枚举值完整性可供穷举 switch

- **WHEN** 变更 4 书写 `CASE_FROM_TO(TextureFormat::R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM)` 穷举表
- **THEN** 本能力域提供的每个 `TextureFormat` 枚举名均可解析

#### Scenario: 采样参数位域可用作哈希键

- **WHEN** 构造两个 `SamplerParams`，仅 `wrapS` 不同
- **THEN** 两者的位模式不同，可作为 `unordered_map` 的键区分

#### Scenario: TextureUsage 位组合

- **WHEN** 计算 `TextureUsage::SAMPLEABLE | TextureUsage::COLOR_ATTACHMENT`
- **THEN** 得到同时含两位置位的值

### Requirement: 像素数据描述符

`include/Backend/BufferDescriptor.h` SHALL 提供 `BufferDescriptor`：CPU 侧数据块描述（`void* buffer` / `size_t size` / 释放回调 `Callback` / `void* user`），移动语义（禁止拷贝），析构时若回调非空 SHALL 调用回调通知调用方数据已消费。

`include/Backend/PixelBufferDescriptor.h` SHALL 提供 `PixelBufferDescriptor`：在 `BufferDescriptor` 基础上增加 `PixelDataFormat` / `PixelDataType` / `CompressedPixelDataType` / `uint32_t stride` / `uint8_t left` / `uint8_t top` 等字段。

`BufferDescriptor` 的 `Callback` SHALL 按上游语义在**数据被消费后**调用（异步上传路径下由 backend 线程触发；同步路径下由调用方保证生命周期）。本能力域只定义类型与移动/析构行为，不定义消费时机策略。

#### Scenario: 移动后源对象为空

- **WHEN** 移动构造 `BufferDescriptor b(std::move(a))`
- **THEN** `b` 持有原指针与回调，`a` 的指针为空；`a` 析构不触发回调

#### Scenario: 析构触发回调

- **WHEN** 持有非空回调的 `BufferDescriptor` 析构
- **THEN** 回调被调用一次，参数为登记的 `user` 指针

### Requirement: 着色器与描述符族类型

`DriverDefine.h` SHALL 提供：

- `ShaderModel` / `ShaderLanguage` / `ShaderStage` / `ShaderStageFlags` / `UniformType` / `ConstantType` / `Precision` / `CompilerPriorityQueue`
- `PushConstantVariant`：`using PushConstantVariant = std::variant<int32_t, float, bool>;`
- `DescriptorFlags` / `descriptor_set_t`（`uint8_t`）/ `descriptor_binding_t`（`uint8_t`）
- `DescriptorSetLayoutDescriptor`：含 `binding` / `type`（`DescriptorType`）/ `count` / `flags` / `stageFlags`
- `DescriptorSetLayout`：含 `setLayout`（`std::array<DescriptorSetLayoutHandle, MAX_DESCRIPTOR_SET_COUNT>`）
- `FeatureLevel` / `TimerQueryResult` / `MapBufferAccessFlags` / `Workaround` / `AsyncCallId` / `StreamType` / `StreamCallback`
- `FrameScheduledCallback`：`using FrameScheduledCallback = std::function<void(PresentCallable)>;`（上游 `utils::Invocable` → `std::function` 映射，见 `backend-utils`）

#### Scenario: PushConstantVariant 携带三种类型

- **WHEN** 分别以 `int32_t{1}` / `float{1.0f}` / `bool{true}` 构造 `PushConstantVariant`
- **THEN** 三次构造均编译通过，`std::get` 可取出原值

### Requirement: TargetBufferInfo 与 MRT

`include/Backend/TargetBufferInfo.h` SHALL 提供：

- `TargetBufferInfo`：含 `Handle<HwTexture> handle` / `uint8_t level` / `uint16_t layer`，提供 4 个构造重载（默认 / handle / handle+level / handle+level+layer）
- `MRT`：含 `static constexpr uint8_t MIN_SUPPORTED_RENDER_TARGET_COUNT = 4;` 与 `MAX_SUPPORTED_RENDER_TARGET_COUNT = 8;`，内部持 `TargetBufferInfo mInfos[MAX_SUPPORTED_RENDER_TARGET_COUNT]`，提供 `operator[]` 与 1/2/3/4 参数的构造重载 + 一个 `(Handle<HwTexture>, level, layer)` 的向后兼容构造

容量常量 SHALL 按上游落在 `MRT` 类内，SHALL NOT 上浮到 `DriverDefine.h`——它们是 `MRT` 自身的契约，不是全局容量。

上游文件末尾的 `#if !defined(NDEBUG)` ostream 重载 SHALL NOT 移植（`utils::io::ostream` 属 Filament 自研流体系，项目改用 spdlog）。

#### Scenario: 逐级构造

- **WHEN** 依次以 `TargetBufferInfo{handle}` / `{handle, 1}` / `{handle, 1, 2}` 构造
- **THEN** 未显式提供的 `level` / `layer` 分别为 0

#### Scenario: MRT 容量正确

- **WHEN** 检查 `MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT`
- **THEN** 等于 8，与上游一致（`VulkanDriver` 的 `MAX_RENDERTARGET_ATTACHMENT_TEXTURES` 由 `MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT * 2 + 1` 推导，故该值不得改动）

#### Scenario: 无 ostream 依赖

- **WHEN** include `Backend/TargetBufferInfo.h` 与 `Backend/Handle.h`
- **THEN** 不引入 `utils/io/ostream` 相关符号

### Requirement: 容量常量集中

`DriverDefine.h` SHALL 集中定义后端容量常量：`MAX_VERTEX_ATTRIBUTE_COUNT = 16`、`MAX_VERTEX_BUFFER_COUNT = 16`（保持现状位置与值）、`MAX_SAMPLER_COUNT`、`MAX_DESCRIPTOR_SET_COUNT`。

`MRT` 的两个容量常量按上游落在 `TargetBufferInfo.h`（见上一条）。`Program::SAMPLER_BINDING_COUNT` 落在 `Program.h`。

SHALL NOT 在多个位置重复定义同一容量。

#### Scenario: 常量单一来源

- **WHEN** 全项目检索 `MAX_DESCRIPTOR_SET_COUNT`
- **THEN** 只有 `DriverDefine.h` 一处定义，其余均为引用

### Requirement: 适配约束
- 头文件保护 SHALL 使用 `#pragma once`
- 命名 SHALL 遵循项目规范：类型 `PascalCase`、枚举值 `PascalCase`、常量 `kPascalCase`、宏 `UPPER_SNAKE_CASE`
  - **特例**：`TextureFormat` / `SamplerType` 等枚举的**值名** SHALL 保留上游 `UPPER_SNAKE_CASE` 拼写（如 `R8G8B8A8_UNORM`）。理由：变更 4 的 `CASE_FROM_TO` 映射表逐值引用，改名会使上游比对失去机械性，且枚举值名承载格式语义，`PascalCase` 化后（`R8g8b8a8Unorm`）可读性反而下降。此偏离须在 tasks 中记录
- Include 顺序 SHALL 遵循 `.dsh/rules/cpp.md`：本项目 `.h` → 本项目 `""` → 第三方 → C++ 标准库 → C 库
- 位标志类枚举 SHALL 提供 `constexpr` 位运算；值恰为 0 的标志（`TargetBufferFlags::NONE`、`BufferUsage::STATIC`）SHALL NOT 提供 `operator bool`
- 注释 SHALL 遵循 `.dsh/rules/code-style.md`：不复述枚举值含义（值名已自明），只对**非自明**的取值（如上游历史遗留的位值混用）说明意图
- 本能力域 SHALL NOT 追加任何 `DriverAPI.inc` 方法声明
- 本能力域 SHALL NOT 提供任何到 Vulkan 类型的映射函数（属变更 4）

#### Scenario: 编译通过

- **WHEN** 构建整个 `Backend` 静态库
- **THEN** 本能力域的全部源文件编译通过，不依赖 Filament 任何头文件

#### Scenario: 命名与风格合规

- **WHEN** 检查本能力域新增的类型与函数
- **THEN** 命名遵循项目规范（`PascalCase` 类型与公有方法、`m_camelCase` 私有成员、`kPascalCase` 常量），头文件使用 `#pragma once`，不保留上游 license / 文件头注释
