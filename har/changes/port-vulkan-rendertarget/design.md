# Design: port-vulkan-rendertarget

## Context

本变更是变更 6/7 的最后一个硬前置。它把「附件 → 渲染通道 → 帧缓冲」这条链补齐：

```
  变更 4 的 VulkanTexture / VulkanAttachment
        │
        ├──────────────────────────────────┐
        ▼                                  ▼
  VulkanSwapChain                     VulkanRenderTarget
  （包装 Platform::SwapChain*）        （附件集合 + rpkey/fbkey）
        │                                  │
        │  GetAttachment()                 │  GetRenderPassKey() / GetFboKey()
        ▼                                  ▼
        └──────────► VulkanFboCache ◄──────┘
                     （VkRenderPass / VkFramebuffer 缓存）
                            │
                            ▼
                 变更 7 的 beginRenderPass / endRenderPass
```

上游关键事实（探索阶段确认）：

- `VulkanSwapChain.cpp:93/102` 用 `VulkanTexture` 的「包装已有 `VkImage`」分支创建颜色与深度附件——这是变更 4 保留该分支的唯一动机。
- `VulkanRenderTarget` 的 `Auxiliary` 含 `utils::bitset32 colors`——按变更 1 的映射约定应使用 `NS_UTILS::Bitset32`。
- `VulkanRenderTarget` 的 default 构造（关联交换链）比 offscreen 简单得多：不需创建任何附件，附件由 `BindSwapChain` 注入。
- `VulkanSwapChain::Acquire` 需要 `VulkanCommands::InjectDependency` —— 既有项目的 `VulkanCommands` **已具备该接口**（`src/vulkan/commands/VulkanCommands.h`），本变更是首次把它接进交换链，不是修改它。
- `VulkanFboCache` 与 `VulkanPipelineCache` 是 `tsl::robin_map` 使用文件中最需要核对 `erase` 语义的两个（design D7 点名）。

## Goals / Non-Goals

**Goals:**

- `VulkanSwapChain` 可创建（headless 与 surface 两条路径）、可销毁、可查询附件
- `VulkanRenderTarget` 的 default 与 offscreen 两个构造可用
- `VulkanFboCache` 可创建/复用 `VkRenderPass` 与 `VkFramebuffer`，`Gc()` 可回收
- `VulkanRenderTarget` 的屏障发射（`EmitBarriersBeginRenderPass` / `EndRenderPass`）可用
- `bin/BackendTests` 不回归

**Non-Goals:**

- 不接线 `VulkanDriver` 的 `createSwapChainR` / `createRenderTargetR` / `createDefaultRenderTargetR` / `beginRenderPass` / `endRenderPass`（属变更 7；本变更只备组件）
- 不移植 `nextSubpass` / 多子通道渲染（`currentSubpass` 字段保留但只有 0 被使用）
- 不实现 MSAA 解析路径的完整语义（`msaaIndex` / `msaaDepthStencilIndex` 字段保留，解析路径的实际调用属变更 7 的 `resolve`）
- 不移植 `VulkanBlitter` 的自动 resolve（变更 6）
- 不移植 Present timing 查询（MoltenVK 不支持，变更 3 已定）

## Decisions

### D1: `VulkanContext.cpp` 的移植边界——先核对既有实现

`VulkanContext.h` 已在项目中（含全部成员与访问器），但 `.cpp` 未移植。上游 `VulkanContext.cpp` 仅 79 行，负责部分特性查询。

**风险**：既有 `VulkanPlatform.cpp`（515 行）的 `queryAndSetDeviceFeatures` 可能已经把上游 `VulkanContext.cpp` 的逻辑吸收进去了。若无脑移植会产生重复实现或符号冲突。

**决策**：实施第一步 SHALL 是「逐行比对上游 `VulkanContext.cpp` 与既有 `VulkanPlatform::queryAndSetDeviceFeatures`」，列出未覆盖的行，只补差集。tasks 中列为第 1 步。

**已否决的替代方案**：直接把上游 `VulkanContext.cpp` 复制过来。否决理由——会与既有实现冲突，且违背「不引入重复定义」。

### D2: `VulkanRenderTarget` 落位 `VulkanHandle.h`，与既有缓冲族同文件

上游所有非纹理 Hw 资源类型集中在 `VulkanHandles.h`（1142 行）。本项目已把缓冲族（`VulkanVertexBufferInfo` / `VulkanBufferObject` / `VulkanIndexBuffer` / `VulkanVertexBuffer`）放在 `src/vulkan/VulkanHandle.h`。

**决策**：`VulkanRenderTarget` 追加到既有的 `src/vulkan/VulkanHandle.h`，不新建文件。

**理由**：与既有落位一致；`VulkanHandle.h` 已是本项目「非纹理 Hw 资源类型」的归属地。变更 6 的 `VulkanProgram` / `VulkanRenderPrimitive` / `VulkanDescriptorSet(Layout)` 同样追加到此处。

**代价**：该文件将增长到约 500 行。可接受——它承载的是同质内容（每个类型约 50–120 行的声明）。

### D3: `Auxiliary` 用 `unique_ptr` 的理由必须保留

上游 `VulkanRenderTarget`：

```cpp
struct Auxiliary {
    VulkanFboCache::RenderPassKey rpkey = {};
    VulkanFboCache::FboKey fbkey = {};
    std::vector<VulkanAttachment> attachments;
    std::array<ColorClearKind, MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT> colorClearKinds = {};
    utils::bitset32 colors;
    int8_t depthStencilIndex = UNDEFINED_INDEX;
    int8_t msaaDepthStencilIndex = UNDEFINED_INDEX;
    int8_t msaaIndex = UNDEFINED_INDEX;
};
std::unique_ptr<Auxiliary> mInfo;
```

`mInfo` 用 `unique_ptr` 而非内联成员，配合 `swap()` 实现移动语义。

**决策**：保留 `unique_ptr<Auxiliary>` 形态与 `swap()` 移动实现，SHALL NOT 改为内联成员。

**理由**：`VulkanRenderTarget` 的移动构造与移动赋值都经 `swap()` 实现（`VulkanFboCache` 需要把 render target 存入容器并移动）。改为内联成员后 `swap` 语义不变，但 `VulkanRenderTarget` 的大小会显著增长（`fbkey` 含 8 个 `VkImageView`），移动成本上升。

**偏离记录**：`utils::bitset32` → `NS_UTILS::Bitset32`（变更 1 的映射约定）。

### D4: `VulkanFboCache` 的 `robin_map` → `unordered_map` 是本变更最高风险点

上游 `VulkanFboCache` 的 `Gc()` 遍历缓存并删除未使用项。design D7（变更 1）明确点名「`erase` 后持有其他元素迭代器」是替换 `robin_map` 的典型踩坑模式。

**决策**：替换前 SHALL 逐行核对 `VulkanFboCache::Gc()` 的循环结构。

**核对清单**：
1. `Gc()` 是否在遍历中 `erase` 当前元素？（`unordered_map` 下 `it = map.erase(it)` 是合法的，`map.erase(it++)` 也是；但若代码写成 `for (auto& [k,v] : map) { if (...) map.erase(k); }` 则是 UB）
2. 回调（如 `VulkanRenderPass` 析构触发的 GC）是否在遍历期间修改容器？
3. 是否有跨 rehash 持有的迭代器或引用？

**处置**：本变更交付时必须给出上述三问的明确答案并记录在 tasks 的复核项中。若发现 UB 模式，SHALL 先修正遍历写法，再替换容器——顺序不可颠倒。

### D5: `VulkanSwapChain` 与 `VulkanRenderTarget` 的所有权关系

上游：

```
VulkanRenderTarget（default 构造）        VulkanSwapChain
   mInfo->colors / attachments  ◄──── BindSwapChain(VulkanSwapChainPtr)
                                          GetAttachment(index, level)
```

`VulkanRenderTarget` 的 default 实例在驱动构造期就创建（上游注释：「We always create the default rendertarget before `createDefaultRenderTarget()`. We swap the content later when `createDefaultRenderTarget()` is called. This frees `createDefaultRenderTarget()` from being ordered with `makeCurrent()`.」）。

**决策**：保留该设计。`VulkanRenderTarget` 的 default 构造 SHALL 不依赖交换链存在；附件由 `BindSwapChain` 在 `makeCurrent` 时注入，由 `ReleaseSwapchain` 在切换时撤销。

**影响**：变更 7 的 `VulkanDriver` 构造期就 `AllocateAndConstruct<VulkanRenderTarget>()`，`createDefaultRenderTargetR` 只是把它交给句柄。此顺序约束须在变更 7 的 design 中引用。

### D6: `MAX_RENDERTARGET_ATTACHMENT_TEXTURES` 的落位

上游定义在 `VulkanDriver.h`：

```cpp
constexpr uint8_t MAX_RENDERTARGET_ATTACHMENT_TEXTURES =
        MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT * 2 + 1;
```

**决策**：落位 `VulkanConstants.h`，`VulkanDriver.h` 引用。

**理由**：它由 `MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT`（变更 2 的 `TargetBufferInfo.h`）推导，是渲染通道附件数的通用约束，被 `VulkanFboCache` 与 `VulkanDriver` 共同消费。放 `VulkanDriver.h` 会使 `VulkanFboCache` 反向依赖驱动头。

### D7: 类型表追加（5 特化 + 4 销毁分支）

| 新类型 | `ResourceType` | 特化落位 | 销毁分支 |
|---|---|---|---|
| `VulkanSwapChain` | `SwapChain` (4) | `Resource.h` / `.cpp` | `ResourceManager.cpp` |
| `VulkanRenderTarget` | `RenderTarget` (3) | 同上 | 同上 |
| `VulkanFramebuffer` | `Framebuffer` (21) | 同上 | 同上 |
| `VulkanRenderPass` | `RenderPass` (22) | 同上 | 同上 |
| `VulkanTexture` | `Texture` (6) | 变更 4 已补 | 变更 4 已补 |

本变更追加 4 特化 + 4 分支（`VulkanTexture` 由变更 4 补）。`DestroyWithType` 分支数由 8（变更 4 后）增至 12。

**注意**：`VulkanFramebuffer` 与 `VulkanRenderPass` 是**内部资源**（由 `VulkanFboCache` 经 `AllocateAndConstruct` 创建，不暴露给客户端句柄）。它们的 `AllocateAndConstruct` 路径不加句柄引用，但 `ResourceManager::Acquire` 仍要求它们是 `Resource` 派生——上游已满足，本项目保持一致。

### D8: 验证策略

1. **全量构建** + `bin/BackendTests` 不回归
2. **headless 渲染目标往返**：在 `tests/Engine` 中创建 `VulkanSwapChain`（headless）→ `VulkanRenderTarget`（default）→ `BindSwapChain` → 查询 `GetRenderPassKey` / `GetFboKey` → `ReleaseSwapchain` → 销毁。验证：
   - `VulkanSwapChain::GetAttachment(0, 0)` 返回有效 `VulkanAttachment`
   - `VulkanRenderTarget::GetExtent()` 与交换链 extent 一致
   - `VulkanFboCache::GetRenderPass(rpkey)` 两次调用返回同一句柄（缓存命中）
   - `GetFramebuffer(fbkey)` 同上
3. **`VulkanFboCache::Gc()` 回收验证**：使若干 `VkRenderPass` / `VkFramebuffer` 的引用归零 → `Gc()` → 再次 `GetRenderPass` 相同 key 返回**不同**句柄（证明旧项已回收）

**固有局限**：`beginRenderPass` / `endRenderPass` 未接线，`EmitBarriersBeginRenderPass` / `EmitBarriersEndRenderPass` 无调用方。这两个屏障路径本变更**不验证**，只以编译通过与上游逐行比对为准。

## Risks / Trade-offs

- [`VulkanContext.cpp` 与既有 `VulkanPlatform` 重复] → D1 要求先比对再补差集，不整体复制
- [`VulkanFboCache::Gc()` 的 `erase` 语义] → D4 的三问核对清单；若发现 UB 模式，先修遍历再换容器
- [`VulkanHandle.h` 增长到约 500 行] → D2 记录为有意取舍；变更 6 会再追加约 300 行（Program / RenderPrimitive / DescriptorSet），最终约 800 行。若届时不可接受，按「Hw 资源树」拆分的独立变更处理
- [`VulkanRenderTarget` 的 `private HwRenderTarget` 继承] → 上游私有继承 `HwRenderTarget`，意味着外部无法经 `VulkanRenderTarget*` 访问 `HwRenderTarget` 成员。本项目须保持一致——`ResourceManager` 的 `HandleCast` 需要能转换，须核对 `HandleAllocator` 的 `Construct` / `HandleCast` 对私有继承的处理
- [`VulkanFramebuffer` / `VulkanRenderPass` 是内部资源但需 `Resource` 派生] → 与既有 `VulkanBuffer` 的处理一致（`AllocateAndConstruct` 路径）；核对 `GetTypeEnum` 特化与 `DestroyWithType` 分支成对
- [交换链在 MoltenVK 上的 headless 行为未验证] → 变更 3 的 tasks 已列该验证；本变更的 `VulkanSwapChain` 测试复用它
- [本变更的屏障路径无调用方] → 以编译期 + 人工比对覆盖，tasks 中记录未验证面

## Open Questions

- `VulkanContext.cpp` 的实际差集大小：若既有 `VulkanPlatform::queryAndSetDeviceFeatures` 已完全覆盖，本变更的该部分为零改动——需在实施第一步确认
- `VulkanRenderTarget` 的私有继承与 `HandleAllocator` 的兼容性：既有 `ResourceManager::Acquire<D>` 用 `HandleCast<D*, B>(handle)`，私有继承下 `static_cast` 的可见性需验证；若不可行，改为 public 继承并记录偏离
- `VulkanFboCache` 是否需要 `Terminate()` 之外的显式 `Gc()` 调用点：上游 `VulkanDriver::tick` 每帧调用 `mFramebufferCache.gc()`，该接线属变更 7
- `VulkanSwapChain` 的 `Recreate()` 触发条件：上游在 `HasResized()` 为真时于 `beginFrame` 路径重建。该逻辑属变更 7

## 实施结论（回填 Open Questions）

1. **`VulkanContext.cpp` 的实际差集 = 空。** 上游该文件根本不含特性查询，全部内容是 7 个 `VulkanAttachment` 访问器（变更 4 已落在 `VulkanTexture.cpp`）。未创建 `src/vulkan/VulkanContext.cpp`。
2. **私有继承 `HwRenderTarget` 与 `HandleAllocator` 兼容，维持 private。** `HandleCast<Dp, B>` 实为「`void*` → `Dp` 的静态向下转型」，不经过基类指针转换，因此不受继承可见性限制；已在 `tests/App.cpp` 中以 `ResourceManager::Acquire<VulkanRenderTarget, HwRenderTarget>` 运行期验证通过。
3. **`Gc()` 无 UB 模式**：删除一律走 `iter = map.erase(iter)`，回调不入容器，循环内只持值的拷贝。容器已直接替换为 `std::unordered_map`（保留自定义 Hash / Equal）。
4. `VulkanFboCache` 的显式 `Gc()` 调用点（每帧一次）与 `VulkanSwapChain` 的 `Recreate()` 触发条件，仍属变更 7；本变更只备组件。
5. 另有两处 spec 未覆盖、实施期必须新增的类型：`VulkanRenderPassContext`（落 `VulkanContext.h`）、`HwRenderTarget` / `HwSwapChain`（落 `src/HwDefine.h`），以及 `IsUnsignedIntFormat` / `IsSignedIntFormat`（落 `include/Backend/DriverDefine.h`）。详见 `tasks.md` 的差异清单第 10–12 条。
