# Tasks: port-vulkan-enums

## 1. math 最小子集（`3rd/Utils`，子模块内，须单独提交）

- [x] 1.1 `include/Utils/math/Vector.h`：定义全局命名空间 `math` 下的 `float2` / `float3` / `float4` / `uint2` / `uint3` / `vec2` / `double4`；每个提供默认（零）构造、逐分量构造、具名分量访问、`operator==` / `operator!=`
- [x] 1.2 `include/Utils/math/Matrix.h`：定义 `math::mat3f`（3×3 float POD，默认构造为单位矩阵 + 逐元素构造）
- [x] 1.3 `tests/`：为 7 个类型补单元测试（覆盖 `backend-math` spec 的各 Scenario）
- [x] 1.4 构建 `3rd/Utils` 并跑 `bin/UtilsTests`，全绿
- [x] 1.5 在 `3rd/Utils` 仓库内单独提交 —— 已完成（随变更 1 的设施提交一并落地）

## 2. DriverDefine.h 渲染通道族

- [x] 2.1 `TargetBufferFlags`（`enum class : uint32_t`）+ `constexpr` 位运算 + `HasAnyFlag`；不提供 `operator bool`
- [x] 2.2 `RenderPassFlags`（`clear` / `discardStart` / `discardEnd`）
- [x] 2.3 `Viewport`（`left` / `bottom` / `width` / `height` 为 `int32_t`）与 `DepthRange`
- [x] 2.4 `using ClearColorValue = math::double4;`
- [x] 2.5 `RenderPassParams`：逐字段对齐上游（含 `flags` / `clearColor` / `clearDepth` / `clearStencil` / `viewport` / `depthRange` / `readOnlyDepthStencil` / `subpassMask`）与静态常量 `READONLY_DEPTH` / `READONLY_STENCIL`
- [x] 2.6 复核：`RenderPassParams` 与上游逐字段比对（字段名、类型、顺序、默认值）

## 3. DriverDefine.h 管线族

- [x] 3.1 `PrimitiveType` / `CullingMode` / `BlendEquation` / `BlendFunction` / `StencilOperation` / `StencilFace`
- [x] 3.2 `PolygonOffset`
- [x] 3.3 `RasterState`（逐字段 + `HasBlending()` 查询）
- [x] 3.4 `StencilState`（`front` / `back` 两组 `StencilStateOps`）
- [x] 3.5 `DescriptorFlags` / `descriptor_set_t` / `descriptor_binding_t` / `DescriptorType` / `DescriptorSetLayoutDescriptor`
- [x] 3.6 `DescriptorSetLayout`（含 `std::array<DescriptorSetLayoutHandle, MAX_DESCRIPTOR_SET_COUNT> setLayout`）
- [x] 3.7 `PipelineState`（逐字段对齐上游）
- [x] 3.8 复核：`PipelineState` / `RasterState` / `StencilState` / `DescriptorSetLayout` **四结构逐字段比对上游**，含默认值与数组形态（对应 design D5）；对照 `VulkanDriver::bindPipelineImpl` 的每个读取点确认可编译

## 4. DriverDefine.h 纹理与采样族

- [x] 4.1 `TextureType` / `SamplerType` / `SamplerFormat` / `SubpassType` / `TextureCubemapFace`
- [x] 4.2 `TextureFormat`（`enum class : uint16_t`，约 90 值）——**逐值与上游比对**，后缀变体不得遗漏
- [x] 4.3 `TextureUsage`（`enum class : uint16_t` 位标志 + 位运算）
- [x] 4.4 `TextureSwizzle`
- [x] 4.5 `SamplerWrapMode` / `SamplerMinFilter` / `SamplerMagFilter` / `SamplerCompareMode` / `SamplerCompareFunc`
- [x] 4.6 `SamplerParams`（`unsigned` 位域打包，**逐位域对齐上游**——它作为 `VulkanSamplerCache` 的哈希键）
- [x] 4.7 复核：`TextureFormat` 枚举值与上游逐值比对；记录本能力域**不提供**到 `VkFormat` 的映射（属变更 4）

## 5. DriverDefine.h 像素、着色器与描述符族

- [x] 5.1 `PixelDataFormat` / `PixelDataType` / `CompressedPixelDataType`
- [x] 5.2 `ShaderModel` / `ShaderLanguage` / `ShaderStage` / `ShaderStageFlags` / `UniformType` / `ConstantType` / `Precision` / `CompilerPriorityQueue`
- [x] 5.3 `using PushConstantVariant = std::variant<int32_t, float, bool>;`
- [x] 5.4 `FeatureLevel` / `TimerQueryResult` / `MapBufferAccessFlags` / `Workaround` / `AsyncCallId` / `StreamType` / `StreamCallback`
- [x] 5.5 ~~`using FrameScheduledCallback = std::function<void(PresentCallable)>;`~~ **跳过**：`PresentCallable` 上游未定义（它由 `FrameScheduledCallback` 所在的前端层提供），单独引入会造出一个无消费者的类型。待变更 7 确认是否需要时再补
- [x] 5.6 **推迟到变更 3**：`Platform` 别名族（`StereoscopicType` / `FrameTimestamps` / `CompositorTiming` / `AsynchronousMode`）依赖 `Platform` 扩展。变更 3 会把这三个枚举的权威定义落进 `Platform.h`，届时 `DriverDefine.h` 改为 `using` 别名
- [x] 5.7 容量常量集中：`MAX_SAMPLER_COUNT` / `MAX_DESCRIPTOR_SET_COUNT` 落 `DriverDefine.h`；复核 `MAX_VERTEX_ATTRIBUTE_COUNT` / `MAX_VERTEX_BUFFER_COUNT` 位置与值不变

## 6. TargetBufferInfo 与描述符

- [x] 6.1 `include/Backend/TargetBufferInfo.h`：`TargetBufferInfo`（4 个构造重载）+ `MRT`（容量常量落类内 + `operator[]` + 5 个构造重载）
- [x] 6.2 确认该头**不**引入 `utils::io::ostream`（上游末尾的 `#if !defined(NDEBUG)` 重载不移植）
- [x] 6.3 `include/Backend/BufferDescriptor.h`：`BufferDescriptor`（移动语义 + 析构回调）
- [x] 6.4 `include/Backend/PixelBufferDescriptor.h`：`PixelBufferDescriptor`（继承 `BufferDescriptor` + 像素字段）
- [x] 6.5 复核：`MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT == 8`（`VulkanDriver` 的 `MAX_RENDERTARGET_ATTACHMENT_TEXTURES` 依赖它）

## 7. Program 构建器

- [x] 7.1 `include/Backend/Program.h`：类声明 + 嵌套类型（`ShaderBlob` / `ShaderSource` / `Descriptor` / `DescriptorSetLayoutBinding` / `Uniform` / `PushConstant` / `SpecializationConstant` 及 `*Info` 别名，容器换 `std::vector`）
- [x] 7.2 `include/Backend/Program.h`：容量常量 `SHADER_TYPE_COUNT = 3` / `UNIFORM_BINDING_COUNT` / `SAMPLER_BINDING_COUNT` 落类内，以 `constexpr` 替代上游的 CMake `CONFIG_*` 宏
- [x] 7.3 `include/Backend/Program.h`：11 个链式 setter 声明（`PascalCase` 化）+ 只读访问器声明；**不移植** `Diagnostics(...)`
- [x] 7.4 `src/Program.cpp`：构造/析构/移动默认实现（定义在 `.cpp`）+ 各 setter 实现（`std::copy_n` 写着色器 bloby）
- [x] 7.5 复核：`Program::DescriptorSetLayoutBinding` 与 `Backend::DescriptorSetLayout` 的完整限定名不冲突，各引用点解析到正确类型
- [x] 7.6 ~~`tests/` 补 `Program` 单测~~ **跳过**：`tests/CMakeLists.txt` 用 `GLOB_RECURSE *.cpp` 收集全部源文件并直接 `add_executable`，放入单测会产生两个 `main`，且为 Backend 引入 gtest 属构建配置改动（本变更范围外）。改以临时翻译单元做等价验证：11 个链式 setter、全部访问器、移动语义、`layoutBinding.layout.descriptors` 的类型解析

## 8. 验证

- [x] 8.1 全量构建（`cmake --build build`）无错误、无新增警告
- [x] 8.2 运行 `bin/BackendTests`：8 帧往返正常、无 `LOG_CRITICAL`、exit 0（确认既有路径零回归）
- [x] 8.3 运行 `bin/UtilsTests`：新增 math 类型用例全绿
- [x] 8.4 ~~运行 `tests/` 的 `Program` 单测~~ **不适用**：见 7.6，`Program` 验证以临时 TU 完成（已删除），未进构建
- [x] 8.5 逐字段比对复核（三个结构族）：`RenderPassParams` / `PipelineState`+`RasterState`+`StencilState`+`DescriptorSetLayout` / `TargetBufferInfo`+`MRT`
- [x] 8.6 逐值比对复核：`TextureFormat`（约 90 值）与 `SamplerParams` 位域布局
- [x] 8.7 **记录局限**：本变更建好的类型在变更 3–7 落地前无任何消费者，"类型是否正确"只能由编译通过 + 人工比对保证，无运行期验证；SHALL NOT 宣称已端到端验证
- [x] 8.8 在 `backend-enums` spec 中登记两处有意偏离：枚举值名保留上游 `UPPER_SNAKE_CASE`（非项目 `PascalCase`）；`math` 类型保留上游小写名

## 实施记录

### 与原规划的偏离

1. **`PipelineState` / `PipelineLayout` 保持普通 struct**（未采用模板化绕法）。根因是 `DriverDefine.h` 与 `Handle.h` 之间存在**潜在循环包含**——`Handle.h` 只为拿 `BEGIN_NS_BACKEND` 等宏而包含了整个 `DriverDefine.h`。已抽出 `include/Backend/Namespace.h` 承载三个命名空间宏，`Handle.h` 改而包含它，依赖变为单向 `DriverDefine.h` → `Handle.h`。附带影响：`Handle.h` 此前靠 `DriverDefine.h` 传递获得 `Utils/Utils.h`，现改为直接包含。
2. **`FrameScheduledCallback` 未移植**（5.5）：`PresentCallable` 上游未定义。
3. **`Platform` 别名族推迟到变更 3**（5.6）。
4. **`CONFIG_UNIFORM_BINDING_COUNT` / `CONFIG_SAMPLER_BINDING_COUNT` 未在 `Program.h` 重新定义**：`DriverDefine.h` 已有同名常量（9 / 4，上游 `DriverEnums.h:141-142` 硬编码），`Program` 直接引用，避免重复定义。
5. **`Program::ShaderLanguage(ShaderLanguage)` 形参名遮蔽类型名**：形参写作 `Backend::ShaderLanguage`，已在 `.cpp` 同步。
6. **`BufferDescriptor` 的 `make<T, &T::method>()` / `make<T>(functor)` 模板未移植**：从项目外部实例化会因 `CallbackHandler` 不完整而编译失败，且当前无消费点。`PixelBufferDescriptor` 同理由此省去 6 个 `make` 重载。

### 未验证面

| 项 | 状态 | 兑现变更 |
|---|---|---|
| 本变更引入的全部类型 | 无消费者——变更 3–7 才使用。正确性由「编译通过 + 与上游逐结构/逐值比对」保证，**无运行期验证**，不得宣称已端到端验证 | 3–7 |
| `Program` | 以临时 TU 验证链式构造/访问器/移动/类型解析，未进构建 | 6–7 |
| `math` 类型 | 已补 7 个单元测试（`3rd/Utils/tests/MathTest.cpp`），随 `bin/UtilsTests` 运行 | — |
| `TextureFormat` 的 90 个枚举值 | 逐值索引比对已完成（`R8 = 0` … `SRGB_ALPHA_BPTC_UNORM = 108`），但到 `VkFormat` 的映射属变更 4 | 4 |
