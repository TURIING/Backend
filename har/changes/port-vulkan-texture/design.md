# Design: port-vulkan-texture

## Context

纹理层是变更 5（交换链 + 渲染目标）的硬前置。依赖链：

```
  变更 2 的 backend-enums（TextureFormat / SamplerParams / TextureUsage）
        │
        ▼
  ┌──────────────────────────────────────────────────────────┐
  │  本变更                                                   │
  │  src/vulkan/utils/  ── Definitions / Conversion / Image   │
  │        │                    / Spirv / Helper / StaticVec  │
  │        ▼                                                  │
  │  VulkanMemory（VkImage 的 VMA 封装）                       │
  │        │                                                  │
  │        ▼                                                  │
  │  VulkanTexture ──┬── VulkanSamplerCache                   │
  │                  └── VulkanYcbcrConversionCache           │
  └──────────────────────┬───────────────────────────────────┘
                         │ 交换链附件 / 渲染目标附件
                         ▼
                  变更 5（VulkanSwapChain / VulkanFboCache / VulkanRenderTarget）
```

上游关键事实（探索阶段确认）：

- `VulkanSwapChain.cpp:93/102` 用 `VulkanTexture` 的「包装已存在 `VkImage`」构造分支创建颜色与深度附件。**这意味着"只做清屏"也绕不开纹理层**——口径 B 与口径 C 的差距因此比表面看起来小得多。
- `VulkanTexture.h` 内含一个 `struct VulkanStream : public HwStream`（视频流），是文件内的独立类型，与纹理主体耦合很弱。
- `VulkanTexture` 有 4 个构造分支，其中第 3 个（AHardwareBuffer / 外部图像）属被砍功能。
- `VulkanStageImage` 位于 `VulkanStagePool.h`（第 131-190 行），既有移植时被砍；`ResourceType::StageImage` 枚举已在 `Resource.h` 预留值 16。
- `VulkanSamplerCache.h` / `VulkanYcbcrConversionCache.h` 是 `tsl::robin_map` 的使用者（各 1 处）。

## Goals / Non-Goals

**Goals:**

- `src/vulkan/utils/` 六个文件到位，成为后续所有组件的类型转换基础
- `VulkanTexture` 可创建（从零 / 包装已有 `VkImage`）与销毁，布局跟踪可用
- `VulkanSamplerCache` / `VulkanYcbcrConversionCache` 可创建与查询
- `VulkanStageImage` 回补，`ResourceType::StageImage` 的类型表与销毁分支闭合
- `bin/BackendTests` 不回归

**Non-Goals:**

- 不移植 `VulkanSwapChain` / `VulkanFboCache` / `VulkanRenderTarget`（变更 5）
- 不移植外部图像路径：`VulkanExternalImageManager` / `createVkImageFromExternal` / AHardwareBuffer 构造分支
- 不移植视频流路径：`VulkanStream` / `VulkanStreamedImageManager`
- 不接线 `VulkanDriver` 的 `createTextureR` / `destroyTexture` / `update3DImage`（属变更 7；本变更只备组件）
- 不提供 GLSL → SPIR-V 编译（`Spirv.h` 只做二进制校验与 `VkShaderModule` 创建辅助）

## Decisions

### D1: `VulkanTexture` 裁掉哪些分支——精确边界

上游 `VulkanTexture` 的 4 个构造分支：

| # | 分支 | 处置 | 理由 |
|---|---|---|---|
| 1 | 从零创建（format/usage/levels/samples） | **保留** | `createTextureR` 路径，是纹理的正路 |
| 2 | 包装已存在的 `VkImage` | **保留** | 交换链附件路径，本变更的核心动机 |
| 3 | AHardwareBuffer / 外部图像 | **砍** | 用户决策砍掉外部图像；依赖 `Platform::ExternalImage`（变更 3 未移植） |
| 4 | 从 `VkImage` + 自定义 `VkImageView` | **保留** | `createTextureView` / `createTextureViewSwizzle` 路径 |

`VulkanStream` 内联类型 SHALL 从 `VulkanTexture.h` 中**整体删除**（含 `mStream` 成员），不保留空壳。`setExternalStream` 的实现属变更 7，届时留空桩。

`Ycbcr` 结构 SHALL **保留**（`VulkanYcbcrConversionCache` 需要），但其中的外部格式分支（`externalFormat` / AHardwareBuffer 相关）SHALL 删除。保留 `VkSamplerYcbcrConversion conversion` 字段本身。

**影响**：`VulkanTexture.h` 的 include 列表中 `<utils/RangeMap.h>`、`<utils/Hash.h>`、`VulkanMemory.h`、`VulkanStagePool.h`、`vulkan/utils/Image.h` 全部需要，本变更一并提供。

### D2: `StaticVector` 保留为独立类型，不用 `std::vector` 替代

上游 `utils/StaticVector.h`（135 行）是一个固定容量的栈上向量。使用点：

- `VulkanCommandBuffer::mWaitSemaphores` / `mWaitSemaphoreStages`（**既有代码已用 `std::array<T, 2>` 替代**）
- `VulkanProgram::BindingList`（`StaticVector<uint16_t, MAX_SAMPLER_COUNT>`）
- `VulkanAwaitHandles`

**决策**：保留 `fvkutils::StaticVector` 作为独立类型（命名空间保持 `fvkutils` 或改为 `VK_UTILS`，与既有 `VkUtils.h` 一致），**不**用 `std::vector` 替代。

**理由**：
1. `VulkanProgram::BindingList` 是命令流命令体的成员路径，要求无堆分配——`std::vector` 会引入分配。
2. 既有的 `VulkanCommandBuffer` 用 `std::array<T, 2>` 替代是可行的（容量固定为 2 且无动态增长），但 `StaticVector` 有 `push_back` 语义，`std::array` 无法等价表达。

**已否决的替代方案**：用 `std::vector` 一统。否决理由——命令体要求无堆分配；且小容量场景下 `std::vector` 的堆分配开销在高频路径上不可忽略。

### D3: 命名空间从 `fvkutils` 改为 `VK_UTILS`

上游 `src/vulkan/utils/` 下的符号位于 `filament::backend::fvkutils`。本项目既有 `src/vulkan/VkUtils.h` 用 `VK_UTILS` 命名空间（含 `enumerate` 等工具）。

**决策**：本变更的六个文件使用 `VK_UTILS` 命名空间，与既有 `VkUtils.h` 合并为同一个工具命名空间。

**理由**：项目已有一个 Vulkan 工具命名空间，再引入 `fvkutils` 会造成「同一职责两个命名空间」。既有 `VkUtils.h` 的 `Enumerate` / `IsVkDepthFormat` 与本变更的 `Definitions` / `Conversion` 职责相近，合并后 `VK_UTILS::Enumerate` 与 `VK_UTILS::GetVkFormat` 自然共存。

**冲突核对**：既有 `VkUtils.h` 已有 `IsVkDepthFormat`，而上游 `Conversion.h` 也有 `isVkDepthFormat`。**须合并为一份**（保留既有实现，删除上游重复定义），在 tasks 中单列核对项。

### D4: `Conversion` 的映射函数用 `CASE_FROM_TO` 宏

`.dsh/rules/cpp.md` 规定「单值映射的 case 用 `CASE_FROM_TO(X, Y)` 压缩，禁止逐 case 手写两行」。上游 `Conversion.cpp` 的 `getVkFormat` 是约 90 个 case 的巨型 `switch`，照搬会产生 180 行。

**决策**：全部单值映射改用 `CASE_FROM_TO`。条件返回值同样适用（宏的 `TO` 可为任意单个返回表达式）。

**效果**：`getVkFormat` 从约 180 行压缩到约 90 行，且新增 `TextureFormat` 时遗漏会被 `return VK_FORMAT_UNDEFINED` 兜底 —— 但**这个兜底会掩盖遗漏**，因此 tasks 中要求逐值比对变更 2 的枚举清单。

### D5: `VulkanStageImage` 的三处连锁改动

回补 `VulkanStageImage` 需要同时改三个文件：

```
VulkanStagePool.h/.cpp   + class VulkanStageImage + AcquireStageImage  ← 主体
Resource.h/.cpp          + template<> ResourceType Resource::GetTypeEnum<VulkanStageImage>() noexcept;
ResourceManager.cpp      + case ResourceType::StageImage: destruct<VulkanStageImage>(...)
```

`ResourceManager::DestroyWithType` 当前有 7 个分支，本变更后为 8 个。`GetTypeEnum` 特化声明放 `Resource.h`（前向声明 `class VulkanStageImage;`），定义放 `Resource.cpp` —— 与既有 6 个特化同构。

**注意**：`VulkanStageImage` 是 `class`（有 `private` 成员），而既有特化对象多为 `struct`。前向声明须用 `class` 关键字。

### D6: `tsl::robin_map` → `std::unordered_map` 的 `erase` 语义核对

`VulkanSamplerCache` 与 `VulkanYcbcrConversionCache` 是本变更内的两个使用者。二者都是**只增不删的缓存**（采样器/转换一旦创建即在池内复用，无 `gc()` 路径），因此 `erase` 语义差异无影响。

**决策**：替换为 `std::unordered_map`，保留自定义 `Hash` 模板参数（`NS_UTILS::Hash<SamplerParams>` 或上游等价物），保留自定义 `Equal`（如有）。在 tasks 中记录「本变更两个使用者无 `erase` 调用，语义差异不适用」。

### D7: `Definitions.h` 的全量 `VkFormat` 数组

上游 `Definitions.h` 含 `constexpr VkFormat ALL_VK_FORMATS[]`（约 250 项）与 `VkFormatList = utils::FixedCapacityVector<VkFormat>`。

**决策**：
- `VkFormatList` → `std::vector<VkFormat>`
- `ALL_VK_FORMATS` 数组完整移植（它是 `VulkanContext` 查询格式支持列表的输入）
- 三个位掩码类型（`DescriptorSetMask` / `UniformBufferBitmask` / `SamplerBitmask`）SHALL 基于变更 1 的 `NS_UTILS::Bitset32` 实现，接口保持上游形态（含 `ForEachSetBit` / `operator[]`）

**理由**：上游 `DescriptorSetMask` 在 `VulkanDescriptorSetCache::commit` 里用 `forEachSetBit` 遍历，在 `bindPipeline` 里用 `descriptorSetMaskTable` 构造——接口形态是硬约束。

### D8: 验证策略——编译期为重、运行期为辅

本变更产出的组件在变更 5/7 接线前无驱动层调用方。验证手段：

1. **编译期穷举检查**：`GetVkFormat` 的 `switch` 在启用 `-Wswitch` 时应无「枚举值未处理」告警——这是 `TextureFormat` 完整性的机械验证。若项目未启用 `-Wswitch`，在本变更内为 `Conversion.cpp` 单独开启。
2. **全量构建 + `bin/BackendTests` 不回归**。
3. **`VulkanStageImage` 往返测试**：`AcquireStageImage` → 回收 → 再次 `AcquireStageImage` 同尺寸，验证复用命中。
4. **`VulkanTexture` 单元测试**：以「包装一个手工创建的 `VkImage`」构造，验证 `GetAttachment` / `GetLayout` / `GetExtent2D` 返回正确值。

**固有局限**：`VulkanTexture` 的完整功能（`LoadImage` / `GenerateMipmaps`）需要 `VulkanCommands` 录制命令，而 `VulkanDriver` 尚未持有 `mCommands`（属变更 7）。这些路径本变更**不验证**，tasks 中明确记录。

## Risks / Trade-offs

- [裁剪 `VulkanTexture` 外部图像分支时误删内部依赖] → D1 表格逐分支列出处置；tasks 要求裁剪后对照 `VulkanSwapChain.cpp:93/102` 的调用点确认「包装已有 `VkImage`」分支完整可用
- [`VK_UTILS` 命名空间合并导致 `IsVkDepthFormat` 重复定义] → D3 规定合并为一份；tasks 单列核对项
- [`CASE_FROM_TO` 压缩后遗漏枚举值被 `return` 兜底掩盖] → tasks 要求逐值比对变更 2 的 `TextureFormat` 清单 + 启用 `-Wswitch`
- [`VulkanStageImage` 是 `class` 而既有特化对象是 `struct`] → 前向声明关键字须为 `class`；遗漏会导致特化声明无法匹配
- [`StaticVector` 与既有 `std::array<T,2>` 并存] → 既有 `VulkanCommandBuffer` 保持不动（用户决策倾向：既有代码不改）；新组件用 `StaticVector`。边界为「既有不动、新增用新」，与 `Bitset` 的处理一致
- [`std::unordered_map` 替换引入哈希一致性问题] → `SamplerParams` 的位域布局已在变更 2 对齐上游（spec 中列为硬约束），哈希输入一致
- [`ALL_VK_FORMATS` 数组约 250 项手抄易错] → 从上游文件机械复制 + 编译期长度断言（`static_assert(sizeof(ALL_VK_FORMATS)/sizeof(VkFormat) == kAllVkFormatCount)`）
- [本变更组件在接线前无运行期验证] → 以编译期穷举 + 三处单元测试组合覆盖，并在 tasks 中记录未验证面

## Open Questions

- `VulkanContext.h` 中 `VulkanAttachment` 的落位：上游定义在 `VulkanContext.h`。本变更提供 `VulkanTexture` 后 `VulkanAttachment`（持 `resource_ptr<VulkanTexture>`）即可定义。是否在本变更落位，还是随变更 5 的 `VulkanRenderTarget` 一起——倾向前者（本变更是它的依赖方），但需确认 `VulkanContext.h` 是否愿意承载该类型
  - **已决（批次 B）**：落位 `src/vulkan/VulkanTexture.h` / `.cpp`，本变更内定义。理由：访问器全部转发到 `VulkanTexture`，需要完整类型；而 `VulkanContext.h` 是被 `VulkanStagePool.h`、`VulkanBufferProxy.h` 广泛包含的叶子头，若在其内引用 `VulkanTexture` 会形成 `VulkanContext.h → VulkanTexture.h → VulkanStagePool.h → VulkanContext.h` 的包含环。放 `VulkanTexture.h` 同时省掉一个 `VulkanContext.cpp`（那是变更 5 的产出）
- `VulkanTexture` 的 `LoadImage` 依赖 `VulkanStageImage` 与 `VulkanCommands`；本变更交付 `VulkanStageImage` 但无 `mCommands`。是否需要在本变更内提供 `VulkanTexture` 的独立可用性验证（例如用一次性 `VkCommandBuffer` 手工录制），还是留到变更 7
- `Conversion.cpp` 中 `TransitionLayout` 的实现依赖 `VulkanLayout` 枚举（`Image.h`）与屏障构造，与 `VulkanBufferProxy` 已有的屏障代码存在功能重叠。是否需要抽取公共屏障构造辅助（会改动既有 `VulkanBufferProxy`），还是保持两处独立
- `-Wswitch` 的启用范围：仅为 `Conversion.cpp` 单独开启，还是项目级开启（可能引发既有代码的大量告警）
