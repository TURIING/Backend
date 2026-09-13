# Change: port-vulkan-platform-swapchain

## Why

交换链是「能看到画面」的唯一出口，而它挂在平台抽象上——`VulkanSwapChain`（变更 5）本身只是 `Platform::SwapChain` 的包装，真正的 `vkCreateSwapchainKHR` / `vkAcquireNextImageKHR` / `vkQueuePresentKHR` 由平台层实现。

项目现状：`Platform` 只有一个纯虚函数 `CreateDriver`（`include/Backend/platform/Platform.h` 13 行）；上游对应物还定义 `SwapChain` / `Fence` / `Stream` / `Sync` 四个不透明句柄、`SyncCallback`、`CompositorTiming`、`FrameTimestamps`，以及 7 个与呈现时序相关的虚函数。`VulkanPlatform` 同样缺 `SwapChainBundle` / `ImageSyncData` / `Customization` / `createSwapChain` / `acquire` / `present` 等一整族接口。

另外 `createVkSurfaceKHR` 的签名与上游不一致：本项目返回 `VkSurfaceKHR`，上游返回 `SurfaceBundle = std::tuple<VkSurfaceKHR, VkExtent2D>`（供 headless 场景返回虚拟 extent）。

**这是变更 5（`VulkanSwapChain`）的硬前置**——`VulkanSwapChain` 的全部方法都转发到 `Platform::SwapChain*`。

## What Changes

### 1. `Platform` 抽象扩展（`include/Backend/platform/Platform.h`）

- 新增四个不透明句柄：`struct SwapChain {}` / `struct Fence {}` / `struct Stream {}` / `struct Sync {}`
- `using SyncCallback = void (*)(Sync* sync, void* userData);`
- 新增 `CompositorTiming`（`compositeInterval` / `compositeDeadlineLatency` / `compositeToPresentLatency` / `expectedPresentLatency` + `INVALID`）
- 新增 `FrameTimestamps`（`vsync` / `expectedPresent` / `actualPresent` / `frameReady` / `frameLatch` + `INVALID` / `PENDING`）
- 新增 `AsynchronousMode` 枚举、`StereoscopicType` 枚举（后者现位于 `DriverDefine.h`，本变更确立权威位置）
- `DriverConfig` 扩展：`featureFlagManager`（指针，可空）、`handleArenaSize`、`disableParallelShaderCompile`、`disableAmortizedShaderCompile`、`disableHandleUseAfterFreeCheck`、`disableHeapHandleTags`、`forceGLES2Context`、`stereoscopicType`、`asynchronousMode`、`gpuContextPriority`
- 新增虚函数（默认实现）：`isCompositorTimingSupported()` / `queryCompositorTiming(...)` / `setPresentFrameId(...)` / `queryFrameTimestamps(...)` / `createSync(...)` / `destroySync(...)` / `pumpEvents()`

**范围限定**：`ExternalImage` / `ExternalImageHandle` / `ExternalImageHandleRef` 整套抽象**不移植**（用户决策：砍掉外部图像）。`setAcquiredImage` / `setupExternalImage` 等方法的签名在 `DriverAPI.inc` 中保留，但平台侧无外部图像支持。

### 2. `VulkanPlatform` 扩展（`include/Backend/platform/VulkanPlatform.h`）

- 新增 `using SwapChainPtr = Platform::SwapChain*;`
- 新增 `struct SwapChainBundle`（`color` / `depth` / `swapchain` / `extent` / `format` / `imageCount` 等）
- 新增 `struct ImageSyncData`（`imageIndex` / `imageAcquiredSemaphore` / `imageReadySemaphore`）
- 新增 `struct Customization`（`gpu` 偏好 + `isSRGBSwapChainSupported`，本项目已有简化版，本变更补全）
- 新增 `struct MiscDeviceFeatures`（设备特性开关集合）
- 新增虚函数：`createSwapChain` / `getSwapChainBundle` / `acquire` / `present` / `recreate` / `hasResized` / `isProtected` / `destroy` / `terminate` / `createSync` / `destroySync`
- 新增 `createVkInstance` / `selectVkPhysicalDevice` / `createVkDevice` 三个可覆写钩子
- `createVkSurfaceKHR` 签名改为返回 `SurfaceBundle = std::tuple<VkSurfaceKHR, VkExtent2D>`
- 新增 `struct VulkanSync : public Platform::Sync`（承载 `std::shared_ptr<VulkanCmdFence>`）
- 保留 `ExternalYcbcrFormat`（`VulkanYcbcrConversionCache` 的实现依赖它，用户决策为保留）
- **不移植**：`ExternalImageMetadata` / `extractExternalImageMetadata` / `copyExternalImageToMemoryYUV` / `createVkImageFromExternal` / `ImageData`

### 3. 交换链实现（`src/vulkan/platform/VulkanPlatformSwapChainImpl.{h,cpp}`）

- `VulkanPlatformSwapChainBase`：持有 `SwapChainBundle` + `VkImage → VkDeviceMemory` 映射，提供 `Acquire` / `Present` / `Recreate` / `HasResized` / `IsProtected` / `QueryCompositorTiming` / `SetPresentFrameId` / `QueryFrameTimestamps` / `Destroy` 虚接口
- `VulkanPlatformSurfaceSwapChain`：基于 `VkSurfaceKHR` 的实现（走 `vkCreateSwapchainKHR`）
- `VulkanPlatformHeadlessSwapChain`：基于虚拟 extent 的 headless 实现
- **跳过全部 `#ifdef __ANDROID__` 分支**（用户决策：只做 macOS / MoltenVK）
- `VK_EXT_metal_surface` 路径经既有 `VulkanPlatformApple::createVkSurfaceKHR` 接入

### 4. `VulkanPlatformApple` 适配

- `createVkSurfaceKHR` 返回值改为 `SurfaceBundle`：surface 查询 `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` 得到 `currentExtent`；headless（`nativeWindow == nullptr`）时返回 `VK_NULL_HANDLE` + 默认 extent

## Capabilities

### New Capabilities

- `vulkan-platform-swapchain`: `VulkanPlatform` 的交换链接口族、`VulkanPlatformSwapChainImpl` 两种实现、`VulkanSync`

### Modified Capabilities

- `platform-abstraction`: `Platform` 新增四个句柄、`CompositorTiming` / `FrameTimestamps`、`DriverConfig` 全字段、7 个呈现时序虚函数；`createVkSurfaceKHR` 签名变更（**BREAKING**）

## Impact

- 修改：
  - `include/Backend/platform/Platform.h`（+句柄族 / +时序结构 / +虚函数）
  - `include/Backend/platform/VulkanPlatform.h`（+`SwapChainBundle` / `ImageSyncData` / `Customization` / `MiscDeviceFeatures` / +11 个虚函数 / `createVkSurfaceKHR` 签名变更）
  - `include/Backend/platform/VulkanPlatformApple.h`（签名同步）
  - `src/vulkan/platform/VulkanPlatform.cpp`（+交换链分发 / +`terminate` / +`VulkanSync`）
  - `src/vulkan/platform/VulkanPlatformApple.cpp`（`createVkSurfaceKHR` 返回 `SurfaceBundle`）
- 新增：
  - `src/vulkan/platform/VulkanPlatformSwapChainImpl.h` / `.cpp`
- 不改：`PlatformFactory`（构造路径不变）、`DriverConfig` 在 `VulkanDriver::Create` 中的消费方式
- 验证：`bin/BackendTests` 既有 headless 路径不回归；新增 headless 交换链的创建/销毁往返（不呈现，只验证 `createSwapChain` → `destroy` 闭环）

**BREAKING 说明**：`createVkSurfaceKHR` 返回类型变更会影响 `PlatformFactory` 之外的任何调用点。当前唯一调用点在 `VulkanDriver` 的 `createSwapChainR` 路径（尚未移植），故实际影响面为零；但须在变更 5 落地时按新签名接线。
