# Capability: vulkan-swapchain

## Purpose

Vulkan 交换链资源对象：包装平台层的 `Platform::SwapChain*`，把 `SwapChainBundle` 的 `VkImage` 转换为 `VulkanTexture` / `VulkanAttachment`，并封装 acquire / present / recreate。上游对应物为 `backend/src/vulkan/VulkanSwapChain.{h,cpp}`（357 行）。

它是渲染目标的附件来源——`VulkanRenderTarget`（default 构造）的附件由 `BindSwapChain` 从本对象注入。

## ADDED Requirements
### Requirement: VulkanSwapChain 资源对象

`src/vulkan/VulkanSwapChain.{h,cpp}` SHALL 定义 `VulkanSwapChain`，public 继承 `Resource`，持 `VulkanTexturePtr`（颜色与深度）：

- 构造：`VulkanSwapChain(const VulkanContextPtr&, const VulkanPlatformPtr&, VkDevice, VkQueue, void* nativeWindow, uint64_t flags, VkExtent2D extent)`
- 析构：释放全部附件 `VulkanTexture` 与底层 `Platform::SwapChain*`
- `Resource.h` SHALL 补 `GetTypeEnum<VulkanSwapChain>` 特化（返回 `ResourceType::SwapChain`）
- `DECLARE_SHARE_PTR_CLASS(VulkanSwapChain)` SHALL 在头文件声明

#### Scenario: 构造后附件可用

- **WHEN** 以 headless 参数构造 `VulkanSwapChain`
- **THEN** 颜色与深度附件均为有效的 `VulkanTexturePtr`，`GetAttachment(0, 0)` 返回有效 `VulkanAttachment`

#### Scenario: 析构释放资源

- **WHEN** `VulkanSwapChain` 引用归零并走 GC 后
- **THEN** 全部附件 `VulkanTexture` 释放（`VkImage` / `VkImageView` 销毁）、`Platform::SwapChain*` 经 `VulkanPlatform::Destroy` 释放，无 VMA 泄漏

### Requirement: 附件访问

`VulkanSwapChain` SHALL 提供：

- `GetAttachment(uint8_t index, uint8_t level) → VulkanAttachment`
- `GetExtent() → VkExtent2D`
- `GetSwapChainBundle() → SwapChainBundle const&`
- `IsFirstRenderPass() const` / `MarkFirstRenderPass()`

`GetAttachment(index, level)` SHALL 的 `index` 含义与上游一致（颜色附件下标；深度附件是否经由同一接口访问须与上游核对）。

#### Scenario: 颜色附件

- **WHEN** 交换链有 N 个颜色附件时调用 `GetAttachment(0, 0)`
- **THEN** 返回的 `VulkanAttachment` 的 `GetImage()` 非空、`GetFormat()` 与 `SwapChainBundle` 的格式一致

#### Scenario: 首次渲染通道标记

- **WHEN** 新创建的交换链调用 `IsFirstRenderPass()`
- **THEN** 返回 true；`MarkFirstRenderPass()` 后返回 false

### Requirement: 交换操作

`VulkanSwapChain` SHALL 提供：

- `Acquire()`：经 `VulkanPlatform::Acquire` 取下一张图像，把 `ImageSyncData` 的 `imageAcquiredSemaphore` 经 `VulkanCommands::InjectDependency` 注入依赖链
- `Present()`：经 `VulkanPlatform::Present` 呈现当前图像，等待 `VulkanCommands::AcquireFinishedSignal()` 返回的完成信号量
- `Recreate()`：经 `VulkanPlatform::Recreate` 重建，并重建全部附件 `VulkanTexture`
- `HasResized()` / `IsProtected()`：转发到 `VulkanPlatform`

`Acquire()` SHALL 是 `VulkanCommands` 的**首个接线点**——既有 `VulkanCommands::InjectDependency(VkSemaphore, VkPipelineStageFlags)` 已具备该接口。

#### Scenario: Acquire 注入依赖

- **WHEN** 调用 `Acquire()`
- **THEN** `VulkanCommands` 收到注入的 `imageAcquiredSemaphore` 与等待阶段，下一次 `Flush()` 等待该信号量

#### Scenario: Present 等待完成信号

- **WHEN** 调用 `Present()`
- **THEN** 使用 `AcquireFinishedSignal()` 返回的信号量调用 `vkQueuePresentKHR`

#### Scenario: Recreate 重建附件

- **WHEN** 调用 `Recreate()`
- **THEN** 旧附件全部释放、新附件按新的 `SwapChainBundle` 重建，`extent` 更新

#### Scenario: Acquire 失败处理

- **WHEN** `VulkanPlatform::Acquire` 返回非 `VK_SUCCESS`（如 `VK_ERROR_OUT_OF_DATE_KHR`）
- **THEN** `Acquire()` 返回 false，调用方（变更 7 的 `beginRenderPass`）据此清空当前渲染通道状态

### Requirement: 适配约束
- `VulkanSwapChain.h` / `.cpp` SHALL 位于 `src/vulkan/`
- Ptr 别名 SHALL 在类定义所在头文件声明；函数参数用 `const Ptr&`
- `SwapChainBundle` 中的原始 Vulkan 句柄（`VkSwapchainKHR` / `VkSemaphore`）为非拥有型，SHALL NOT 在本能力域销毁
- 上游文件头注释与 license 注释 SHALL 删除
- 注释 SHALL 遵循 `.dsh/rules/code-style.md`：对非平凡逻辑（`Acquire` 的信号量注入时机、`Recreate` 的附件重建顺序）说明意图
- 本能力域 SHALL NOT 移植 Present timing 查询（`QueryFrameTimestamps` / `QueryCompositorTiming`），MoltenVK 不支持

#### Scenario: 编译通过

- **WHEN** 构建整个 `Backend` 静态库
- **THEN** 本能力域的全部源文件编译通过，不依赖 Filament 任何头文件

#### Scenario: 命名与风格合规

- **WHEN** 检查本能力域新增的类型与函数
- **THEN** 命名遵循项目规范（`PascalCase` 类型与公有方法、`m_camelCase` 私有成员、`kPascalCase` 常量），头文件使用 `#pragma once`，不保留上游 license / 文件头注释
