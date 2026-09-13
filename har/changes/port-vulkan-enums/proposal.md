# Change: port-vulkan-enums

## Why

`include/Backend/DriverDefine.h` 是 143 个驱动方法的参数与返回类型的来源。上游对应物 `backend/include/backend/DriverEnums.h` 约 1780 行、含 50 个类型；本项目现有 9 个（`BackendType` / `StereoscopicType` / `GpuContextPriority` / `FenceStatus` / `BufferUsage` / `BufferObjectBinding` / `ElementType` / `DriverConfig` / `Attribute`）。

**没有这一层，任何驱动方法签名都写不出来**——`beginRenderPass` 需要 `RenderPassParams`、`bindPipeline` 需要 `PipelineState`、`createTexture` 需要 `TextureFormat` + `SamplerType` + `TextureUsage`、`scissor` 需要 `Viewport`。这是变更 3–7 全部组件的编译前提。

同时暴露一个此前未被处理的依赖：上游 `DriverEnums.h` 直接使用 6 个 `math::` 类型——`math::double4`（`ClearColorValue`）、`math::float2`（`getClipSpaceParams()` 返回）、`math::uint3`（`dispatchCompute` 参数）、`math::uint2`（6 处）、`math::vec2`（2 处）、`math::mat3f`（`setAcquiredImage` 参数）。**项目没有 math 库**，这 6 个类型必须先有落脚点。

## What Changes

### 1. math 最小子集（`3rd/Utils`，子模块内提交）

- 新增 `include/Utils/math/Vector.h`：`math::float2` / `float3` / `float4` / `uint2` / `uint3` / `vec2` / `double4`
- 新增 `include/Utils/math/Matrix.h`：`math::mat3f`（仅当变更 6 保留 `setAcquiredImage` 时需要；否则本变更不引入）
- 命名空间为**全局 `math`**（非 `NS_UTILS::math`），使上游调用点零改写
- **不移植** Filament `libmath` 的其余内容（矩阵/四元数/几何/噪声等逾 5000 行）

### 2. 类型层扩展（`include/Backend/`）

- `DriverDefine.h` 按上游 `DriverEnums.h` 的分节顺序整体扩展，新增约 40 个类型：
  - 渲染通道族：`TargetBufferFlags` / `RenderPassFlags` / `RenderPassParams` / `Viewport` / `DepthRange` / `ClearColorValue`
  - 管线族：`PipelineState` / `RasterState` / `StencilState` / `PolygonOffset` / `PrimitiveType` / `CullingMode` / `BlendEquation` / `BlendFunction` / `StencilOperation` / `StencilFace`
  - 纹理族：`TextureFormat`（约 90 枚举值）/ `TextureUsage` / `TextureSwizzle` / `TextureType` / `TextureCubemapFace` / `SamplerType` / `SamplerParams` / `SamplerWrapMode` / `SamplerMinFilter` / `SamplerMagFilter` / `SamplerCompareMode` / `SamplerCompareFunc` / `SamplerFormat` / `SubpassType`
  - 像素族：`PixelDataFormat` / `PixelDataType` / `CompressedPixelDataType`
  - 描述符族：`DescriptorSetLayout` / `DescriptorSetLayoutDescriptor` / `DescriptorFlags` / `descriptor_set_t` / `descriptor_binding_t`
  - 着色器族：`ShaderModel` / `ShaderLanguage` / `ShaderStage` / `ShaderStageFlags` / `UniformType` / `ConstantType` / `Precision` / `CompilerPriorityQueue` / `PushConstantVariant`
  - 其他：`FeatureLevel` / `TimerQueryResult` / `MapBufferAccessFlags` / `Workaround` / `AsyncCallId` / `StreamType` / `StreamCallback` / `FrameScheduledCallback`
  - 类型别名：`StereoscopicType` / `FrameTimestamps` / `CompositorTiming` / `AsynchronousMode`（由 `Platform` 别名引入）
- 新增 `include/Backend/TargetBufferInfo.h`：`TargetBufferInfo` + `MRT`（`MIN_SUPPORTED_RENDER_TARGET_COUNT = 4`、`MAX_SUPPORTED_RENDER_TARGET_COUNT = 8`）
- 新增 `include/Backend/BufferDescriptor.h` / `PixelBufferDescriptor.h`：CPU 侧数据描述符（含释放回调 `Callback`）
- `include/Backend/DriverDefine.h` 的 `MAX_VERTEX_ATTRIBUTE_COUNT` / `MAX_VERTEX_BUFFER_COUNT` / `MAX_SAMPLER_COUNT` / `MAX_DESCRIPTOR_SET_COUNT` 等容量常量补齐

### 3. Program 构建器（`include/Backend/Program.h` + `src/Program.cpp`）

- `Backend::Program`：着色器二进制（GLSL/SPIR-V/Metal）载体 + descriptor set 布局声明 + push constant 声明 + 优先级队列
- `Program::ShaderBlob`、`Program::DescriptorSetLayout` 绑定列表
- 上游 `src/Program.cpp` 114 行，逐行移植

**BREAKING**：`DriverDefine.h` 单文件增长至约 1800 行。以「贴合上游单文件结构、避免 40+ 处 include 改动」为取舍（见 design D1）。

## Capabilities

### New Capabilities

- `backend-math`: `math::` 6 类型最小子集与命名空间决策
- `backend-enums`: `DriverDefine.h` 全量类型扩展、`TargetBufferInfo`、`BufferDescriptor` / `PixelBufferDescriptor`
- `backend-program`: `Program` 构建器

### Modified Capabilities

（无：`DriverDefine.h` 现有 9 个类型不变，纯追加）

## Impact

- 新增（子模块 `3rd/Utils`，须在子模块内单独提交）：
  - `include/Utils/math/Vector.h`、`include/Utils/math/Matrix.h`
- 新增（本项目）：
  - `include/Backend/TargetBufferInfo.h`
  - `include/Backend/BufferDescriptor.h`
  - `include/Backend/PixelBufferDescriptor.h`
  - `include/Backend/Program.h`
  - `src/Program.cpp`
- 修改：
  - `include/Backend/DriverDefine.h`（+约 40 个类型与容量常量）
- CMake：`src/Program.cpp` 由 `file(GLOB_RECURSE src/*.cpp)` 自动收集
- 不改：`DriverAPI.inc`（本变更只备类型，不追加方法清单——方法随组件同步追加，见变更 3–7）
- 不改：`Driver.h` / `CommandStream.h` / `Dispatcher.h`（宏展开路径与新类型无耦合）
- 验证：本变更无可运行的新增行为，以「全量构建通过 + `bin/BackendTests` 不回归」为判据；`Program` 的构造与查询在 `3rd/Utils` 或 `tests/` 补单元测试
