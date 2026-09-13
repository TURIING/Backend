# Design: port-vulkan-enums

## Context

上游 `backend/include/backend/DriverEnums.h` 是本变更的移植源，约 1780 行、50 个类型。它的角色是「全部驱动方法的参数与返回类型的唯一来源」——`DriverAPI.inc` 的 143 条声明中，除 `int` / `uint32_t` / `Handle<...>` 外的每一个类型都来自这里。

依赖关系：

```
                     ┌─────────────────────────────┐
                     │  math::double4 / float2 /   │  ← 项目缺失，必须先补
                     │  uint2 / uint3 / vec2       │
                     └──────────────┬──────────────┘
                                    │ ClearColorValue / getClipSpaceParams()
                                    ▼
   ┌───────────────────────────────────────────────────────────┐
   │  DriverEnums.h（本项目 DriverDefine.h）                     │
   │  − 渲染通道族 / 管线族 / 纹理族 / 像素族 / 描述符族 / 着色器族  │
   └───────────────┬───────────────────────────┬───────────────┘
                   │                           │
                   ▼                           ▼
   ┌───────────────────────────┐   ┌───────────────────────────┐
   │ TargetBufferInfo.h        │   │ Program.h                 │
   │ TargetBufferInfo / MRT    │   │ 着色器二进制 + 布局声明       │
   └───────────────────────────┘   └───────────────────────────┘
```

下游消费者（变更 3–7）：`DriverAPI.inc` 的 128 条待补声明、`VulkanPipelineCache`（`PipelineState` / `RasterState` / `StencilState`）、`VulkanTexture`（`TextureFormat` / `SamplerParams`）、`VulkanFboCache`（`TargetBufferFlags` / `RenderPassParams`）、`VulkanRenderTarget`（`MRT` / `TargetBufferInfo`）。

现状：项目 `DriverDefine.h` 139 行、9 个类型。这 9 个类型**全部保留且不改**，本变更为纯追加。

## Goals / Non-Goals

**Goals:**

- 补齐 143 个驱动方法签名所需的全部参数与返回类型
- 为 6 个 `math::` 类型提供本地落脚点，使上游调用点零改写
- `Program` 构建器可用（着色器二进制 + descriptor set 布局 + push constant 声明的载体）
- 本变更可独立构建，`bin/BackendTests` 不回归

**Non-Goals:**

- 不追加任何 `DriverAPI.inc` 方法声明（类型备好即可，方法随各自组件同步追加）
- 不实现 `Program` 的编译逻辑（GLSL → SPIR-V 由前端负责；后端只接收二进制）
- 不移植 `utils::io::ostream` 的 `operator<<` 调试输出（上游 `DriverEnums.h` 末尾有 `#if !defined(NDEBUG)` 的 ostream 重载）
- 不移植 Filament `libmath` 的其余内容（矩阵/四元数/几何/噪声逾 5000 行）
- 不引入 `FeatureLevel` 相关的特性探测（`VulkanDriver::getFeatureLevel` 的实现属变更 7）
- 不做 `driver-interface` spec 的修改（方法清单变更留给变更 3–7）

## Decisions

### D1: 单文件 `DriverDefine.h`，不按域拆分

**决策**：全部新类型追加进现有 `include/Backend/DriverDefine.h`，保持单文件；不新建 `TextureDefine.h` / `PipelineDefine.h` / `RenderPassDefine.h`。

**理由**：

1. **对照上游成本最低**。上游是单文件 `DriverEnums.h`，逐节顺序一致时，逐行比对只需打开两个文件。按域拆成三个头文件后，每次同步上游都要在三个文件间来回跳转。
2. **现有 40+ 处 include 零改动**。`DriverDefine.h` 被 `Driver.h` / `Handle.h` / `HwDefine.h` / `Common.h` / `DriverAPI.inc` 等广泛包含；若拆分成多个头并在 `DriverDefine.h` 中聚合，等于多一层转发，收益仅是可导航性。
3. 文件内容以纯枚举与 POD 为主，编译开销可控；且该头已被广泛包含，实际编译单元数不会因拆分而减少。

**已否决的替代方案**：按「纹理 / 管线 / 渲染通道」拆三个头文件 + `DriverDefine.h` 聚合。否决理由——收益（文件短）不足以覆盖成本（对照上游变难、多一层转发、聚合头仍需被全部包含）。

**遗留处置**：若变更 7 落地后实测编译时间成为问题，按域拆分作为独立变更处理；本变更在 Open Questions 中记录。

### D2: math 类型放 `3rd/Utils`，命名空间用全局 `math`

**决策**：头文件位于 `3rd/Utils/include/Utils/math/Vector.h`，类型定义在**全局命名空间 `math`**。

**理由**：

1. **上游调用点零改写**。上游 `DriverEnums.h` 写 `math::double4`、`VulkanDriver.cpp` 写 `math::uint3 workGroupCount`。若定义为 `NS_UTILS::math`，则每处都要改写或加 `namespace math = NS_UTILS::math;` 别名——前者是噪音改动，后者污染全局命名空间且与「不改变实现」相悖。
2. **放 `3rd/Utils` 而非本项目**。这些类型与后端领域无关，属通用几何原语；且 `3rd/Utils` 已有 `Soa.h` 等同类先例。
3. 项目命名规范对命名空间要求 `snake_case`；`math` 是单个小写单词，合规。

**已否决的替代方案 A**：引入 Filament `libmath` 作为 `3rd/` 下的 submodule。否决理由——只需 6 个类型，而 `libmath` 含矩阵/四元数/几何/噪声逾 5000 行，且自带 `tsl/robin_map` 等依赖；为一个 `float2` 拉进整库不成比例。

**已否决的替代方案 B**：定义为本项目的 `Backend::Float2` 等。否决理由——每处调用点都要改写，且这些原语不属于后端领域。

**范围限定**：只提供实际被上游代码引用的运算子集（构造、分量访问、基本算术），不追求与上游 `math::` 完全等价的 API 面。缺失的运算在后续变更遇到时按需补，不预置。

### D3: `mat3f` 是否引入取决于 `setAcquiredImage` 是否保留

上游 `math::mat3f` 的全部 4 处使用都在 `setAcquiredImage` / `VulkanSync` 的外部图像路径（`Platform::ExternalImage` / `AHardwareBuffer`）。用户决策为**砍掉 `VulkanExternalImageManager` + `VulkanStreamedImageManager`**。

但 `setAcquiredImage` 是 `DriverAPI.inc` 的 SYNC 方法之一，签名含 `math::mat3f const& transform`。若保留该方法声明（哪怕实现为空桩），`mat3f` 就必须存在。

**决策**：本变更**引入** `math::mat3f`（约 30 行的 3×3 浮点矩阵 POD + 单位矩阵构造），使 `setAcquiredImage` 的签名可写；其实现体在变更 6/7 按「砍掉外部图像」的决策留空桩。理由——砍掉的是实现，不是接口；`DriverAPI.inc` 的完整性对「143 方法全量对齐」这个目标是硬约束。

### D4: `DriverEnums.h` 末尾的 ostream 重载不移植

上游 `DriverEnums.h` 与 `TargetBufferInfo.h` 末尾有：

```cpp
#if !defined(NDEBUG)
utils::io::ostream& operator<<(utils::io::ostream& out, const filament::backend::TargetBufferInfo& tbi);
#endif
```

`utils::io::ostream` 是 Filament 自研流体系（`ostream.cpp` 550 行）。项目用 spdlog + `LOG_*` 宏。

**决策**：不移植，删除这些声明与对应的 `ostream.cpp` 依赖。调试输出改由 `LOG_DEBUG` 直接完成。记录为有意偏离。

**影响**：上游任何 `<<` 输出 `TextureFormat` / `PipelineState` 的代码（主要在 `VulkanDriver` 的 `BVK_DEBUG_*` 分支里）在移植时须改写为 `LOG_*` 形式。改动点集中在变更 7 的调试分支。

### D5: `DescriptorSetLayout` 与 `PipelineState` 的成员一致性是硬约束

`PipelineState` 与 `DescriptorSetLayout` 是跨组件的契约：

```
   DriverAPI bindPipeline(PipelineState const&)      ← 变更 7 调用
        │
        ▼
   VulkanDriver::bindPipelineImpl(...)               ← 变更 7 实现
        │  读 pipelineState.pipelineLayout.setLayout / .program /
        │    .vertexBufferInfo / .rasterState / .stencilState /
        │    .polygonOffset / .primitiveType
        ▼
   VulkanPipelineCache::RasterState（本地结构）        ← 变更 6 定义
        │  fvkutils::getCullMode(getCullMode) 等转换    ← 变更 4 的 Conversion
        ▼
   VkGraphicsPipelineCreateInfo
```

`PipelineState` 的成员**顺序与命名必须与上游逐字一致**——`bindPipelineImpl` 里有一处 `mPipelineState = {}` 整体重置，以及 `std::transform(setLayouts.begin(), setLayouts.end(), layoutHandles.begin(), ...)` 依赖 `setLayout` 的数组形态。任何成员名或类型偏差都会在变更 6/7 引发连锁改写。

**决策**：`PipelineState` / `DescriptorSetLayout` / `RasterState` / `StencilState` 四个结构 SHALL 逐字段对齐上游，包括默认值。在 tasks 中单列一条「逐字段比对」复核项。

### D6: `TextureFormat` 的 90 个枚举值与 `CASE_FROM_TO` 映射表分家

上游 `TextureFormat` 约 90 个枚举值。把它映射到 `VkFormat` 的函数（`fvkutils::getVkFormat`）在上游属 `src/vulkan/utils/Conversion.cpp`——**不在本变更范围**（属变更 4）。

**决策**：本变更只移植枚举定义本身，不移植任何映射函数。枚举值与上游逐字一致（含 `_SNORM` / `_SSCALED` / `_USCALED` / `_SRGB` / `_INT` 等后缀变体），因为变更 4 的映射表是**按枚举值穷举的 `switch`**，缺一个或拼错一个都会在变更 4 触发 `case` 不匹配。

### D7: 容量常量集中定义，不散落

上游散落多处：`MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT`（`TargetBufferInfo.h`）、`Program::SAMPLER_BINDING_COUNT`（`Program.h`）、`MAX_VERTEX_ATTRIBUTE_COUNT`（`DriverEnums.h`）。

项目现状：`MAX_VERTEX_ATTRIBUTE_COUNT` / `MAX_VERTEX_BUFFER_COUNT` 已在 `DriverDefine.h`。

**决策**：
- `MAX_VERTEX_ATTRIBUTE_COUNT` / `MAX_VERTEX_BUFFER_COUNT` 保持现状位置与值
- 新增 `MAX_SAMPLER_COUNT` / `MAX_DESCRIPTOR_SET_COUNT` / `MAX_VERTEX_ATTRIBUTE_COUNT` 相关常量，落在 `DriverDefine.h`（与既有两个成组放置）
- `MRT::MIN/MAX_SUPPORTED_RENDER_TARGET_COUNT` 按上游落在 `TargetBufferInfo.h` 的 `MRT` 类内——它们是 `MRT` 的私有契约，不是全局容量

理由：符合 `.dsh/rules/cpp.md`「仅适用于本功能的定义存放在该功能范围内的定义文件中」。`MRT` 的容量是 `MRT` 自身的约束，上浮到 `DriverDefine.h` 反而失去语义归属。

### D8: 验证策略——编译期为主

本变更不引入可运行的新行为（纯类型定义 + `Program` 数据载体）。验证手段：

1. **全量构建**。这是主要判据——179 个类型定义与 143 个方法签名的编译兼容性只能由编译器验证。
2. **`bin/BackendTests` 不回归**（既有 8 帧往返仍 exit 0）。
3. **`Program` 单元测试**：构造 + 查询 descriptor set 布局 + push constant 范围。落在 `tests/`。
4. **与上游逐字段比对复核**：`PipelineState` / `DescriptorSetLayout` / `RasterState` / `StencilState` / `RenderPassParams` / `TargetBufferInfo` 六个结构（D5）。

**固有局限**：本变更建好的类型**在变更 3–7 落地前没有任何消费者**，因此"类型是否正确"只能由编译通过与人工比对保证，无法由运行期验证。须在 tasks 中明确记录，不得宣称"已验证"。

## Risks / Trade-offs

- [`DriverDefine.h` 增长到约 1800 行] → 记录为有意取舍（D1）；编译时间若成问题，按域拆分作为独立变更
- [`math` 全局命名空间污染] → 只放 6 个 POD 类型；若与将来引入的第三方 math 库冲突，改用 `NS_MATH` 别名 + 上游调用点批量替换（一次机械改动）
- [`TextureFormat` 枚举值与上游偏差] → 变更 4 的 `getVkFormat` 是穷举 `switch`，缺项会在编译期被 `-Wswitch` 捕获（若启用）；tasks 中要求逐值比对
- [`PipelineState` 字段偏差在变更 6/7 才暴露] → D5 要求在 tasks 中单列逐字段比对复核项，在变更 2 内解决而非拖延
- [`Program` 的 `DescriptorSetLayout` 与 `DriverEnums.h` 的 `DescriptorSetLayout` 同名] → 上游 `backend::Program::DescriptorSetLayout`（内部结构）与 `backend::DescriptorSetLayout`（公共枚举/结构）是**两个不同类型**，命名空间嵌套关系须准确移植，否则变更 6 的 `createProgramR` 会解析到错误类型
- [ostream 重载不移植导致上游调试分支需改写] → 改动集中在变更 7 的 `BVK_DEBUG_*` 分支，已记录（D4）
- [本变更无可运行消费者，验证强度天然偏弱] → 以「编译全绿 + 六结构逐字段比对 + `Program` 单测」三点组合，并在变更 3–7 首次使用这些类型时接受"补漏"成本

## Open Questions

- `DriverDefine.h` 的拆分阈值：若变更 7 落地后实测编译时间明显增长（例如全量构建增量 > 20%），是否按「纹理 / 管线 / 渲染通道」拆分为三个头文件 + 聚合头
- `math` 命名空间与将来可能引入的第三方数学库的冲突预案：是否预先定义 `NS_MATH` 宏别名以便一次性切换
- `Program` 的 GLSL → SPIR-V 编译是否需要在后端提供：上游由前端（`filament` 的 shader compiler）产出 SPIR-V，后端只接收；若本项目计划直接写 GLSL 并运行时编译，需要额外的 `glslang` 依赖，属独立变更
- `TextureFormat` 的枚举值与真实 `VkFormat` 覆盖度：上游枚举中包含若干仅用于前端映射的中间值，变更 4 移植 `getVkFormat` 时若发现上游存在映射缺口，须回头确认本变更的枚举是否漏项
