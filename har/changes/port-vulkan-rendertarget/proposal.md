# Change: port-vulkan-rendertarget

## Why

渲染目标是「把像素写到哪里」的承载，交换链是「写到屏幕」的唯一出口。二者合起来构成 `beginRenderPass` / `endRenderPass` 的完整对象模型——**这是变更 7 的 `beginRenderPass` 实现能够成立的全部前置**。

上游对应组件：

| 组件 | 行数 | 职责 |
|---|---|---|
| `VulkanConstants.h` | 230 | 全局常量与调试开关（`FVK_MAX_COMMAND_BUFFERS` 之外的 Vulkan 侧常量） |
| `VulkanContext.cpp` | 79 | 特性查询的剩余实现（`VulkanContext.h` 已移植，`.cpp` 未移植） |
| `VulkanFboCache.{h,cpp}` | 563 | `VkRenderPass` 与 `VkFramebuffer` 的缓存（键为 `RenderPassKey` / `FboKey`） |
| `VulkanSwapChain.{h,cpp}` | 357 | 交换链资源对象：包装 `Platform::SwapChain*`，构造附件 `VulkanTexture` |
| `VulkanHandles::VulkanRenderTarget` | 约 200（在 1142 行的 `VulkanHandles.h` 内） | 渲染目标资源对象：附件集合 + `RenderPassKey` / `FboKey` + 屏障发射 |

本变更是**变更 6（管线与描述符）与变更 7（驱动收口）的最后一个硬前置**。

## What Changes

### 1. `VulkanConstants.h`（230 行）

- Vulkan 侧常量：`FVK_MAX_COMMAND_BUFFERS` 等既有常量的对侧定义、缓冲区大小上限、附件数上限
- `MAX_RENDERTARGET_ATTACHMENT_TEXTURES = MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT * 2 + 1`（上游定义在 `VulkanDriver.h`，本变更确定其落位）
- 调试开关宏（`BVK_DEBUG_*`）的集中定义

### 2. `VulkanContext.cpp`（79 行）

- 补全 `VulkanContext` 的构造与特性填充逻辑（`VulkanContext.h` 已在项目中，`.cpp` 未移植）
- 需要与既有 `VulkanPlatform` 的特性查询对接——**须核对既有 `VulkanPlatform` 是否已把 `VulkanContext` 填满**，避免重复实现

### 3. `VulkanFboCache`（563 行）

- `RenderPassKey`：`color` / `depth` 附件的格式与采样数、`clear` / `discardStart` / `discardEnd`、`subpassMask`、`initialDepthStencilLayout`、`needsResolveMask`、`samples`
- `FboKey`：`renderPass` / 附件 `VkImageView` / `extent` / `layers` / `samples`
- `VulkanRenderPass` 资源对象（持 `VkRenderPass`）
- `VulkanFramebuffer` 资源对象（持 `VkFramebuffer`）
- `GetRenderPass(RenderPassKey, ResourceManager*)` / `GetFramebuffer(FboKey, ResourceManager*, VulkanRenderTarget*)`
- `Gc()` / `Terminate()`
- `tsl::robin_map` → `std::unordered_map`（**须核对 `Gc()` 的 `erase` 语义**——这是 design D7 点名的高风险点之一）

### 4. `VulkanSwapChain`（357 行）

- 包装 `Platform::SwapChain*`，持 `SwapChainBundle`
- 构造时给颜色与深度附件各创建一个 `VulkanTexture`（**用变更 4 的「包装已有 `VkImage`」分支**）
- `Acquire()` / `Present()` / `Recreate()` / `HasResized()` / `IsProtected()` / `Destroy()`
- `GetAttachment(uint8_t index, uint8_t level)` / `IsFirstRenderPass()` / `MarkFirstRenderPass()`
- 依赖 `VulkanCommands`（`Acquire` 需要 `InjectDependency`）——**既有项目已有 `VulkanCommands`**，本变更首次把它接进交换链

### 5. `VulkanRenderTarget`（约 200 行）

从上游 `VulkanHandles.h` 摘出到本项目的 `VulkanHandle.h`（与既有缓冲族同文件）：

- `VulkanRenderTarget : private HwRenderTarget, public Resource`
- 两个构造：offscreen（带附件）与 default（关联交换链）
- `Auxiliary` 内部结构：`rpkey` / `fbkey` / `attachments` / `colorClearKinds` / `colors`（`NS_UTILS::Bitset32`）/ `depthStencilIndex` / `msaaDepthStencilIndex` / `msaaIndex`
- 访问器：`GetExtent()` / `GetColor(idx)` / `GetColorClearKind(idx)` / `GetDepthStencil()` / `GetRenderPassKey()` / `GetFboKey()` / `GetSamples()` / `GetColorTargetCount(pass)` / `HasDepthStencil()` / `IsSwapChain()` / `IsProtected()`
- `BindSwapChain(VulkanSwapChainPtr)` / `ReleaseSwapchain()` / `IsSwapchainBound()`
- `EmitBarriersBeginRenderPass(VulkanCommandBuffer&)` / `EmitBarriersEndRenderPass(VulkanCommandBuffer&)`
- `TransformClientRectToPlatform(VkRect2D*)` / `TransformViewportToPlatform(VkViewport*)`

## Capabilities

### New Capabilities

- `vulkan-swapchain`: `VulkanSwapChain` 资源对象与附件构造
- `vulkan-rendertarget`: `VulkanConstants` / `VulkanContext.cpp` / `VulkanFboCache` / `VulkanRenderTarget`

### Modified Capabilities

（无。`vulkan-resource` 的类型表在本变更追加 5 条特化与 4 条销毁分支，但那些改动由本变更新增类型驱动，属追加性质，归入 `vulkan-rendertarget` 能力域的适配约束条目而非独立 MODIFIED。）

## Impact

- 新增：
  - `src/vulkan/VulkanConstants.h`
  - `src/vulkan/VulkanContext.cpp`
  - `src/vulkan/VulkanFboCache.h` / `.cpp`
  - `src/vulkan/VulkanSwapChain.h` / `.cpp`
- 修改：
  - `src/vulkan/VulkanHandle.h` / `VulkanHandle.cpp`（+`VulkanRenderTarget`）
  - `src/vulkan/resource/Resource.h` / `Resource.cpp`（+`GetTypeEnum` 特化 ×5）
  - `src/vulkan/resource/ResourceManager.cpp`（+销毁分支 ×4）
  - `include/Backend/VulkanDriver.h` 或 `VulkanConstants.h`（`MAX_RENDERTARGET_ATTACHMENT_TEXTURES` 落位）
- CMake：新增源文件由 `file(GLOB_RECURSE src/*.cpp)` 自动收集
- 不改：`VulkanCommands`（已有，本变更是首次接线而非修改）、`VulkanStagePool`、`VulkanBufferCache`
- 验证：`bin/BackendTests` 不回归；新增 headless 渲染目标的创建-销毁往返（`createDefaultRenderTarget` 等效路径）

**风险集中点**：`VulkanFboCache` 的 `RenderPassKey` / `FboKey` 哈希与 `Gc()` 的 `erase` 语义（`robin_map` → `unordered_map` 的高风险替换点之一）。
