# Capability: vulkan-platform-swapchain

## Purpose

Vulkan 平台的交换链实现层：把 `VkSurfaceKHR` / 原生窗口转换为可用的 `VkSwapchainKHR` + `VkImage` 集合，并暴露 acquire / present / recreate 三个核心操作。上游对应物为 `backend/include/backend/platforms/VulkanPlatform.h` 的交换链部分 + `backend/src/vulkan/platform/VulkanPlatformSwapChainImpl.{h,cpp}`（662 行）。

本能力域是 `VulkanSwapChain`（变更 5）的硬前置：`VulkanSwapChain` 只是 `Platform::SwapChain*` 的包装，全部实际操作转发到这里。

**范围限定**：只做 macOS / MoltenVK 路径，`#ifdef __ANDROID__` 分支整体不移植。

## ADDED Requirements
### Requirement: SwapChainBundle 与 ImageSyncData

`VulkanPlatform` SHALL 定义：

- `using SwapChainPtr = Platform::SwapChain*;`
- `struct SwapChainBundle`：**逐字段对齐上游实际定义**——`std::vector<VkImage> colors` / `VkImage depth` / `VkFormat colorFormat` / `VkFormat depthFormat` / `VkExtent2D extent` / `uint32_t layerCount = 1` / `bool isProtected = false`

  （本 spec 初稿曾写成「颜色附件集合含 VkImageView / VkImageUsageFlags、外加 VkSwapchainKHR 与 imageCount」，那是对上游的**推测**，与 `/Users/turiing/filament` 实际不符——上游不持有 `VkImageView`（视图由变更 5 的 `VulkanTexture` 创建）、不持有 `VkSwapchainKHR`、也不叫 `imageCount`。实施阶段已按上游更正。）
- `struct ImageSyncData`：**逐字段对齐上游实际定义**——`static constexpr uint32_t INVALID_IMAGE_INDEX = UINT32_MAX` / `uint32_t imageIndex` / `VkSemaphore imageReadySemaphore`

  （初稿写的 `imageAcquiredSemaphore` 上游已不存在，不补造死字段。）

两结构 SHALL 逐字段对齐上游——`SwapChainBundle` 被 `VulkanSwapChain` 直接消费（构造 `VulkanAttachment` 的 `VulkanTexture`），`ImageSyncData` 被 `VulkanSwapChain::Acquire` 消费。

#### Scenario: Bundle 可构造 VulkanTexture

- **WHEN** 读取 `bundle.color[i].image` / `.view` / `.format` / `.usage`
- **THEN** 每个访问点编译通过，语义与变更 5 构造 `VulkanTexture` 的需求一致

#### Scenario: imageCount 反映真实交换链

- **WHEN** 对 headless 交换链调用 `getSwapChainBundle()`
- **THEN** `imageCount >= 2`（双缓冲下限）

### Requirement: 交换链虚接口

`VulkanPlatform` SHALL 提供以下虚函数（均带默认实现或转发到内部交换链对象）：

- `virtual SwapChainPtr createSwapChain(void* nativeWindow, uint64_t flags = 0, VkExtent2D extent = {0, 0})`
- `virtual SwapChainBundle getSwapChainBundle(SwapChainPtr handle)`
- `virtual VkResult acquire(SwapChainPtr handle, ImageSyncData* outImageSyncData)`
- `virtual VkResult present(SwapChainPtr handle, uint32_t index, VkSemaphore finishedDrawing)`
- `virtual bool hasResized(SwapChainPtr handle)`
- `virtual bool isProtected(SwapChainPtr handle)`
- `virtual VkResult recreate(SwapChainPtr handle)`
- `virtual void destroy(SwapChainPtr handle)`
- `virtual void terminate()`
- `virtual Platform::Sync* createSync(std::shared_ptr<VulkanCmdFence> fenceStatus) noexcept`
- `virtual void destroySync(Platform::Sync* sync) noexcept`

每个转发函数 SHALL 对空句柄做防御（`if (handle == nullptr) return ...;`），使未创建交换链时的调用退化为 no-op 而非崩溃。

#### Scenario: 空句柄安全

- **WHEN** 对 `nullptr` 调用 `acquire` / `present` / `hasResized` / `isProtected` / `destroy`
- **THEN** 不崩溃，`acquire` / `present` 返回 `VK_ERROR_UNKNOWN` 或等效错误码，`hasResized` / `isProtected` 返回 false

#### Scenario: 创建后可查询 Bundle

- **WHEN** `createSwapChain(nullptr, 0, {1280, 720})` 返回非空句柄后调用 `getSwapChainBundle(handle)`
- **THEN** `extent` 为 `{1280, 720}`、`format != VK_FORMAT_UNDEFINED`、`imageCount >= 2`

### Requirement: createVkSurfaceKHR 返回 SurfaceBundle

`VulkanPlatform` SHALL 定义 `using SurfaceBundle = std::tuple<VkSurfaceKHR, VkExtent2D>;`，`createVkSurfaceKHR` 的签名 SHALL 改为：

```cpp
virtual SurfaceBundle createVkSurfaceKHR(void* nativeWindow, VkInstance instance,
        uint64_t flags) const noexcept = 0;
```

返回的 `VkExtent2D` SHALL 为：
- `nativeWindow != nullptr`：经 `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` 查询 `currentExtent`；若 `currentExtent` 为 `VK_EXTENT_2D_UNDEFINED`（0xFFFFFFFF）则回退到 `minImageExtent`
- `nativeWindow == nullptr`（headless）：`VkSurfaceKHR` 为 `VK_NULL_HANDLE`，`VkExtent2D` 为调用方建议值或默认 `{0, 0}`

`VulkanPlatformApple` SHALL 实现该签名，`VK_EXT_metal_surface` 路径保持不变。

**BREAKING**：本项目此前的签名为返回裸 `VkSurfaceKHR`。当前零调用方，变更影响面为零。

#### Scenario: 有窗口时返回有效 surface 与 extent

- **WHEN** 传入非空 `CAMetalLayer*` 调用 `createVkSurfaceKHR`
- **THEN** 返回的 `VkSurfaceKHR` 非空，`VkExtent2D` 的宽高均大于 0

#### Scenario: headless 时返回空 surface

- **WHEN** 传入 `nullptr` 调用
- **THEN** 返回的 `VkSurfaceKHR` 为 `VK_NULL_HANDLE`，调用方据此走 headless 交换链路径

### Requirement: 交换链两种实现

`src/vulkan/platform/VulkanPlatformSwapChainImpl.{h,cpp}` SHALL 定义：

- `VulkanPlatformSwapChainBase : public Platform::SwapChain`：持有 `VkDevice` / `VkQueue` / `VulkanContext const&` / `SwapChainBundle` / `std::unordered_map<VkImage, VkDeviceMemory>`；提供纯虚 `Acquire` / `Present` / `Recreate` / `HasResized` / `IsProtected` 与虚 `QueryCompositorTiming` / `SetPresentFrameId` / `QueryFrameTimestamps` / `Destroy`；提供受保护 `CreateImage(extent, format, isProtected)`
- `VulkanPlatformSurfaceSwapChain`：基于 `VkSurfaceKHR`，走 `vkCreateSwapchainKHR` / `vkAcquireNextImageKHR` / `vkQueuePresentKHR`
- `VulkanPlatformHeadlessSwapChain`：不依赖 surface，用 `CreateImage` 直接创建 `VkImage`

**`#ifdef __ANDROID__` 分支 SHALL 全部删除**（不留空骨架）。相关 Android 头文件（`AndroidSwapChainHelper.h` / `AndroidNativeWindow.h`）SHALL NOT 引入。

`QueryCompositorTiming` / `SetPresentFrameId` / `QueryFrameTimestamps` SHALL 返回 `false`（MoltenVK 不支持 `VK_GOOGLE_display_timing`）。

#### Scenario: headless 交换链不依赖 surface

- **WHEN** 以 `VK_NULL_HANDLE` surface 构造 `VulkanPlatformHeadlessSwapChain`
- **THEN** 创建成功，`SwapChainBundle::swapchain` 为 `VK_NULL_HANDLE` 但 `color` 附件非空

#### Scenario: 无 Android 依赖

- **WHEN** 在 macOS 上编译 `VulkanPlatformSwapChainImpl.cpp`
- **THEN** 不引用 `ANativeWindow` / `AndroidSwapChainHelper` / `AHardwareBuffer` 任何符号

#### Scenario: 时序查询返回 false

- **WHEN** 调用 `QueryCompositorTiming(&out)` / `QueryFrameTimestamps(0, &out)`
- **THEN** 返回 false，输出参数不被写入

#### Scenario: Destroy 释放全部资源

- **WHEN** 对已创建的交换链调用 `Destroy()`
- **THEN** `VkSwapchainKHR` 经 `vkDestroySwapchainKHR` 释放、`mMemory` 中所有 `VkDeviceMemory` 经 `vmaFreeMemory` 释放、全部 `VkImageView` 释放，无 VMA 泄漏断言

### Requirement: VulkanSync 平台侧实现

`VulkanPlatform` SHALL 定义 `struct VulkanSync : public Platform::Sync`，持有 `std::shared_ptr<VulkanCmdFence> fenceStatus`。

`createSync` SHALL 返回 `new VulkanSync{...}`，`destroySync` SHALL `delete` 传入指针（空指针安全）。

**命名冲突约束**：平台侧类型为 `Platform::VulkanSync`（嵌套），驱动侧 `Backend::VulkanSync`（定义于变更 6 的 `VulkanAsyncHandles.h`，继承 `HwSync` 并内含 `Platform::Sync*`）是**另一个类型**。在 `Backend` 命名空间下裸写 `VulkanSync` 解析到驱动侧；引用平台侧 SHALL 写全 `Platform::VulkanSync`。

#### Scenario: createSync / destroySync 往返

- **WHEN** `createSync(fence)` 返回非空指针后调用 `destroySync(p)`
- **THEN** 不崩溃、无泄漏；`destroySync(nullptr)` 同样安全

#### Scenario: 两个 VulkanSync 不混淆

- **WHEN** 在 `Backend` 命名空间下同时引用驱动侧 `VulkanSync` 与平台侧 `Platform::VulkanSync`
- **THEN** 两者解析到不同类型，编译通过

### Requirement: VulkanPlatform 可覆写钩子

`VulkanPlatform` SHALL 把既有的实例/设备创建逻辑提取为可覆写钩子：

- `virtual VkInstance createVkInstance(VkInstanceCreateInfo const& createInfo) noexcept`
- `virtual VkPhysicalDevice selectVkPhysicalDevice(VkInstance instance) noexcept`
- `virtual VkDevice createVkDevice(VkDeviceCreateInfo const& createInfo) noexcept`

默认实现 SHALL 保持既有行为（直接调用对应的 `vkCreate*` / 既有设备选择逻辑）。提取的目的是让子类可在不重写整个初始化流程的前提下定制。

#### Scenario: 默认行为不变

- **WHEN** 不覆写三个钩子，走既有初始化流程
- **THEN** 实例/物理设备/逻辑设备的创建结果与提取前完全一致（`bin/BackendTests` 不回归）

#### Scenario: 子类可定制

- **WHEN** 子类覆写 `selectVkPhysicalDevice` 返回指定设备
- **THEN** 后续逻辑使用该设备，无需重写初始化流程

### Requirement: 适配约束
- `SwapChainBundle` / `ImageSyncData` / `Customization` / `MiscDeviceFeatures` / `VulkanSync` SHALL 定义在 `include/Backend/platform/VulkanPlatform.h`（公共层，`VulkanSwapChain` 需消费）
- `VulkanPlatformSwapChainImpl.{h,cpp}` SHALL 位于 `src/vulkan/platform/`（实现层）
- 命名 SHALL 遵循项目规范：类型 `PascalCase`、虚函数 `PascalCase`、成员 `m_camelCase`、常量 `kPascalCase`
- `std::unordered_map<VkImage, VkDeviceMemory>` SHALL 按 `backend-utils` 的映射约定使用 `std::unordered_map`（上游此处即为 `std::unordered_map`，非 `robin_map`）
- 上游 `using namespace bluevk;` SHALL 删除——本项目用 `volk`，无需命名空间导入
- 上游文件头注释与 license 注释 SHALL 删除（`.dsh/rules/code-style.md`）
- `#ifdef __ANDROID__` 分支 SHALL 整体删除，不留空骨架
- `SwapChainBundle` 的附件结构 SHALL 与变更 5 构造 `VulkanAttachment` / `VulkanTexture` 的需求逐字段核对

#### Scenario: 编译通过

- **WHEN** 构建整个 `Backend` 静态库
- **THEN** 本能力域的全部源文件编译通过，不依赖 Filament 任何头文件

#### Scenario: 命名与风格合规

- **WHEN** 检查本能力域新增的类型与函数
- **THEN** 命名遵循项目规范（`PascalCase` 类型与公有方法、`m_camelCase` 私有成员、`kPascalCase` 常量），头文件使用 `#pragma once`，不保留上游 license / 文件头注释
