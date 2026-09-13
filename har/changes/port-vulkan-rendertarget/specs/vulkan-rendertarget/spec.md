# Capability: vulkan-rendertarget

## Purpose

渲染目标的完整对象模型：全局常量（`VulkanConstants`）、上下文特性填充（`VulkanContext.cpp`）、渲染通道与帧缓冲缓存（`VulkanFboCache`）、渲染目标资源对象（`VulkanRenderTarget`）。上游对应约 1072 行。

本能力域是变更 7 的 `beginRenderPass` / `endRenderPass` 能够成立的全部前置。

## ADDED Requirements
### Requirement: VulkanConstants 全局常量

`src/vulkan/VulkanConstants.h` SHALL 集中定义 Vulkan 侧常量：

- `MAX_RENDERTARGET_ATTACHMENT_TEXTURES`：`= MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT * 2 + 1`（由变更 2 的 `MRT` 推导）
- 命令缓冲数量上限、附件数上限、缓冲区大小上限等上游 `VulkanConstants.h` 中的常量
- 调试开关宏（`BVK_DEBUG_*` 族）的集中定义

`MAX_RENDERTARGET_ATTACHMENT_TEXTURES` SHALL 落位本文件而非 `VulkanDriver.h`——它由 `VulkanFboCache` 与 `VulkanDriver` 共同消费，放驱动头会使缓存反向依赖驱动。

#### Scenario: 常量单一来源

- **WHEN** 检索 `MAX_RENDERTARGET_ATTACHMENT_TEXTURES`
- **THEN** 只有 `VulkanConstants.h` 一处定义

#### Scenario: 与 MRT 容量一致

- **WHEN** 检查 `MAX_RENDERTARGET_ATTACHMENT_TEXTURES`
- **THEN** 等于 `8 * 2 + 1 = 17`

### Requirement: VulkanContext 特性填充

`src/vulkan/VulkanContext.cpp` SHALL 提供 `VulkanContext.h` 中已声明但尚未实现的成员定义。

**实施前置**：SHALL 先逐行比对上游 `VulkanContext.cpp` 与既有 `src/vulkan/platform/VulkanPlatform.cpp` 的 `queryAndSetDeviceFeatures`，列出未覆盖的差集，只补差集。

若差集为空，本项 SHALL 记录为「既有实现已完全覆盖，本文件不新增」而非创建空文件。

#### Scenario: 无重复实现

- **WHEN** 检索特性查询逻辑（如 `m_depthStencilFormats` / `m_isUnifiedMemoryArchitecture` 的填充点）
- **THEN** 每个成员的填充只出现在一处

#### Scenario: 编译无重复符号

- **WHEN** 全量构建
- **THEN** 无 `VulkanContext` 成员的重复定义链接错误

### Requirement: VulkanFboCache 渲染通道与帧缓冲缓存

`src/vulkan/VulkanFboCache.{h,cpp}` SHALL 提供：

- `struct RenderPassKey`（上游字段）：颜色附件格式与采样数、深度附件格式与采样数、`clear` / `discardStart` / `discardEnd`（`TargetBufferFlags`）、`subpassMask`、`initialDepthStencilLayout`（`VulkanLayout`）、`needsResolveMask`、`samples`
- `struct FboKey`：`renderPass`（`VkRenderPass`）、颜色/深度 `VkImageView` 数组、`extent`、`layers`、`samples`
- `VulkanRenderPass` 资源对象（持 `VkRenderPass`，public 继承 `Resource`）
- `VulkanFramebuffer` 资源对象（持 `VkFramebuffer`，public 继承 `Resource`）
- `GetRenderPass(RenderPassKey const&, ResourceManager*) → VulkanRenderPassPtr`
- `GetFramebuffer(FboKey const&, ResourceManager*, VulkanRenderTarget*) → VulkanFramebufferPtr`
- `Gc()` / `Terminate()`

`RenderPassKey` / `FboKey` SHALL 各提供哈希函数与判等函数，作为 `std::unordered_map` 的模板参数（上游为 `tsl::robin_map`，按 `backend-utils` 映射约定替换，**保留自定义 Hash 与 Equal**）。

`GetRenderPass` / `GetFramebuffer` SHALL 命中缓存时返回同一对象（引用计数 +1），未命中时经 `AllocateAndConstruct` 创建并插入。

#### Scenario: 相同 key 命中缓存

- **WHEN** 以相同 `RenderPassKey` 两次调用 `GetRenderPass`
- **THEN** 两次返回指向同一 `VulkanRenderPass` 的指针

#### Scenario: 不同 clear 标志产生不同 RenderPass

- **WHEN** 两个 key 仅 `clear` 字段不同
- **THEN** 返回两个不同的 `VkRenderPass`

#### Scenario: Gc 回收未使用项

- **WHEN** 某 `VulkanRenderPass` 引用归零后调用 `Gc()`，再以相同 key 调用 `GetRenderPass`
- **THEN** 返回**新的** `VkRenderPass`（证明旧项已回收）

#### Scenario: Terminate 释放全部

- **WHEN** 调用 `Terminate()`
- **THEN** 全部 `VkRenderPass` / `VkFramebuffer` 经 `vkDestroy*` 释放，无泄漏

### Requirement: VulkanRenderTarget 资源对象

`src/vulkan/VulkanHandle.h` / `.cpp` SHALL 定义 `VulkanRenderTarget`：

- **私有继承** `HwRenderTarget`，public 继承 `Resource`（与上游一致）
- 构造 1：offscreen（`VkDevice` / `VkPhysicalDevice` / `VulkanContextPtr` / `ResourceManager*` / `VmaAllocator` / `VulkanCommands*` / width / height / samples / 颜色附件数组 / 深度附件 / `VulkanStagePool&` / layerCount）
- 构造 2：default（无参数，关联交换链）
- 移动构造与移动赋值经 `swap()` 实现
- `Resource.h` SHALL 补 `GetTypeEnum<VulkanRenderTarget>` 特化（返回 `ResourceType::RenderTarget`）

`Auxiliary` 内部结构 SHALL 含：`rpkey` / `fbkey` / `std::vector<VulkanAttachment> attachments` / `std::array<ColorClearKind, MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT> colorClearKinds` / `NS_UTILS::Bitset32 colors` / `depthStencilIndex` / `msaaDepthStencilIndex` / `msaaIndex`。`mInfo` SHALL 为 `std::unique_ptr<Auxiliary>`。

`enum class ColorClearKind : uint8_t { Float, SignedInt, UnsignedInt }` SHALL 保留——`VulkanDriver::beginRenderPass` 据此分发 `VkClearColorValue` 的 union 分支，写错会产生静默的像素损坏。

#### Scenario: default 构造不依赖交换链

- **WHEN** 无参数构造 `VulkanRenderTarget`
- **THEN** 构造成功，附件为空，`IsSwapChain()` 返回 true

#### Scenario: BindSwapChain 注入附件

- **WHEN** 对 default 实例调用 `BindSwapChain(swapChainPtr)`
- **THEN** 附件从交换链注入，`GetExtent()` 等于交换链 extent，`IsSwapchainBound()` 返回 true

#### Scenario: ReleaseSwapchain 撤销附件

- **WHEN** 调用 `ReleaseSwapchain()`
- **THEN** 附件清空，`IsSwapchainBound()` 返回 false

#### Scenario: 移动语义

- **WHEN** 移动构造一个已绑定交换链的 `VulkanRenderTarget`
- **THEN** 目标对象持有全部状态，源对象为空（`mInfo` 为 nullptr 状态），无双重释放

### Requirement: 渲染目标查询接口

`VulkanRenderTarget` SHALL 提供：

- `GetExtent()` / `GetSamples()` / `HasDepthStencil()` / `IsSwapChain()` / `IsProtected()`
- `GetColor(uint32_t idx)` / `GetColorClearKind(uint32_t idx)` / `GetDepthStencil()`
- `GetRenderPassKey()` / `GetFboKey()`
- `GetColorTargetCount(VulkanRenderPassContext const&)`
- `IsSwapchainBound()`
- `TransformClientRectToPlatform(VkRect2D*)` / `TransformViewportToPlatform(VkViewport*)`

全部访问器 SHALL 逐一对齐上游签名与语义——变更 7 的 `beginRenderPass` 逐字段读取这些值。

#### Scenario: 查询集合覆盖 beginRenderPass 的需求

- **WHEN** 对照上游 `VulkanDriver::beginRenderPass` 的每个 `rt->` 调用点
- **THEN** 每个方法均存在于本能力域，签名一致

#### Scenario: 坐标变换

- **WHEN** 调用 `TransformClientRectToPlatform(&rect)`
- **THEN** rect 按平台差异（MoltenVK 的 Y 轴翻转）被就地修改，与上游行为一致

### Requirement: 渲染通道屏障发射

`VulkanRenderTarget` SHALL 提供：

- `EmitBarriersBeginRenderPass(VulkanCommandBuffer&)`
- `EmitBarriersEndRenderPass(VulkanCommandBuffer&)`

二者 SHALL 录制图像布局转换屏障：进入渲染通道时把附件从旧布局转到 `COLOR_ATTACHMENT` / `DEPTH_STENCIL_ATTACHMENT`；离开时转到可采样布局（`SHADER_READABLE` / `DEPTH_SAMPLER`）。

#### Scenario: 进入时转换颜色附件布局

- **WHEN** 对布局为 `VK_IMAGE_LAYOUT_UNDEFINED` 的颜色附件调用 `EmitBarriersBeginRenderPass`
- **THEN** 录制一条屏障，目标布局为 `COLOR_ATTACHMENT_OPTIMAL`

#### Scenario: 离开时转换到可采样布局

- **WHEN** 调用 `EmitBarriersEndRenderPass`
- **THEN** 录制的屏障目标布局为 `SHADER_READ_ONLY_OPTIMAL`（或上游定义的等效值）

#### Scenario: 无附件时不发射

- **WHEN** 对未绑定交换链的 default 实例调用 `EmitBarriersBeginRenderPass`
- **THEN** 不录制任何命令，不崩溃

### Requirement: 适配约束
- `VulkanConstants.h` / `VulkanContext.cpp` / `VulkanFboCache.{h,cpp}` SHALL 位于 `src/vulkan/`
- `VulkanRenderTarget` SHALL 追加到既有 `src/vulkan/VulkanHandle.h` / `.cpp`（与缓冲族同文件），不新建文件
- Ptr 别名 SHALL 在类定义所在头文件声明；函数参数用 `const Ptr&`
- `utils::bitset32` SHALL 替换为 `NS_UTILS::Bitset32`（变更 1 映射约定）
- `tsl::robin_map` SHALL 替换为 `std::unordered_map`，**保留自定义 Hash 与 Equal**
- **`Gc()` 的 `erase` 语义核对**：替换容器前 SHALL 逐行核对遍历结构，确认不存在「遍历中按 key 删除」或「回调期间修改容器」的模式；若存在，先修正遍历写法再替换
- `Resource.h` / `Resource.cpp` / `ResourceManager.cpp` SHALL 补 4 条特化与 4 条销毁分支（`SwapChain` / `RenderTarget` / `Framebuffer` / `RenderPass`）
- 上游文件头注释与 license 注释 SHALL 删除；`using namespace bluevk;` SHALL 删除
- 注释 SHALL 遵循 `.dsh/rules/code-style.md`：对非平凡逻辑（`ColorClearKind` 的分发依据、屏障的布局选择、`swap()` 的移动实现）说明意图

#### Scenario: 编译通过

- **WHEN** 构建整个 `Backend` 静态库
- **THEN** 本能力域的全部源文件编译通过，不依赖 Filament 任何头文件

#### Scenario: 命名与风格合规

- **WHEN** 检查本能力域新增的类型与函数
- **THEN** 命名遵循项目规范（`PascalCase` 类型与公有方法、`m_camelCase` 私有成员、`kPascalCase` 常量），头文件使用 `#pragma once`，不保留上游 license / 文件头注释
