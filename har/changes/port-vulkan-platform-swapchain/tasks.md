# Tasks: port-vulkan-platform-swapchain

## 1. Platform 头部解耦

- [x] 1.1 `include/Backend/platform/Platform.h`：移除 `#include "Backend/Driver.h"`，改为 `DECLARE_CLASS_AND_SHARE_PTR(Driver)` + 前置声明，使 `DriverPtr` 可见而不引入 `Driver.h`
- [x] 1.2 全量构建确认无循环 include、无未解析的 `DriverPtr`

## 2. Platform 句柄族与时序结构

- [x] 2.1 新增 `struct SwapChain {}` / `struct Fence {}` / `struct Stream {}` / `struct Sync {}`
- [x] 2.2 新增 `using SyncCallback = void (*)(Sync* sync, void* userData);`
- [x] 2.3 新增 `CompositorTiming`（`time_point_ns` / `duration_ns` / `INVALID` + 四个 `duration_ns` 字段）
- [x] 2.4 **按上游更正**：task 原写「五个 time_point_ns 字段（vsync/expectedPresent/actualPresent/frameReady/frameLatch）」是推测；上游实为 `requestedPresentTime` / `acquireTime` / `latchTime` 等。已按 `/Users/turiing/filament` 实际移植，spec 同步更正
- [x] 2.5 前置声明 `struct VulkanCmdFence;`（**不** include `src/` 下的头文件）
- [x] 2.6 新增 `pumpEvents()` 虚函数，默认返回 `false`

## 3. 枚举权威位置与 DriverConfig

- [x] 3.1 `Platform.h` 定义 `StereoscopicType` / `GpuContextPriority` / `AsynchronousMode` 三个枚举（权威位置）
- [x] 3.2 `DriverDefine.h`：删除这三个枚举的定义，改为 `using StereoscopicType = Platform::StereoscopicType;` 等三个别名；include `Backend/platform/Platform.h`
- [x] 3.3 `Platform.h`：`DriverConfig` 补齐全字段（`featureFlagManager` / `metalUploadBufferSizeBytes` / `disableAmortizedShaderCompile` / `disableHandleUseAfterFreeCheck` / `disableHeapHandleTags` / `forceGLES2Context` / `asynchronousMode` / `gpuContextPriority` 等），默认值对齐上游
- [x] 3.4 处理 `featureFlagManager` 字段的类型：若 `FeatureFlagManager` 尚不存在，以 `void const*` 占位并注明（变更 7 建立后改强类型）
- [x] 3.5 复核：`DriverDefine.h` 现有的 `DriverConfig` 定义与 `Platform.h` 的新定义不重复——**只保留 `Platform::DriverConfig` 一处**，`VulkanDriver::Create` 的消费点改为引用 `Platform::DriverConfig`
- [x] 3.6 全量构建 + 跑 `bin/BackendTests`，确认既有驱动创建路径零回归

## 4. Platform 呈现时序虚函数

- [x] 4.1 `isCompositorTimingSupported()` / `queryCompositorTiming(...)` / `setPresentFrameId(...)` / `queryFrameTimestamps(...)`：默认返回 `false`
- [x] 4.2 `createSync(std::shared_ptr<VulkanCmdFence>)` 默认返回 `nullptr`；`destroySync(Sync*)` 默认空实现
- [x] 4.3 复核：`include/Backend/platform/Platform.h` 不引入 `src/` 下任何头文件

## 5. VulkanPlatform 交换链类型

- [x] 5.1 `VulkanPlatform.h`：新增 `using SwapChainPtr = Platform::SwapChain*;`
- [x] 5.2 **按上游更正**：上游 `SwapChainBundle` 实为 `colors`(VkImage 向量) / `depth` / `colorFormat` / `depthFormat` / `extent` / `layerCount` / `isProtected`，不含 `VkImageView` / `VkSwapchainKHR` / `imageCount`。已按上游实现并用上游 `VulkanSwapChain::update()` 的逐字段消费点核对，spec 同步更正
- [x] 5.3 **按上游更正**：上游 `ImageSyncData` 已无 `imageAcquiredSemaphore`，只有 `INVALID_IMAGE_INDEX` / `imageIndex` / `imageReadySemaphore`。不补造死字段，spec 同步更正
- [x] 5.4 补全 `Customization`（`gpu` 偏好 + `isSRGBSwapChainSupported`，本项目已有简化版）
- [x] 5.5 新增 `struct MiscDeviceFeatures`
- [x] 5.6 新增 `using SurfaceBundle = std::tuple<VkSurfaceKHR, VkExtent2D>;`
- [x] 5.7 新增 `struct VulkanSync : public Platform::Sync`（持 `std::shared_ptr<VulkanCmdFence>`）
- [x] 5.8 新增 11 个交换链虚函数声明（`createSwapChain` / `getSwapChainBundle` / `acquire` / `present` / `hasResized` / `isProtected` / `recreate` / `destroy` / `terminate` / `createSync` / `destroySync`）
- [x] 5.9 新增 3 个可覆写钩子声明（`createVkInstance` / `selectVkPhysicalDevice` / `createVkDevice`）
- [x] 5.10 `createVkSurfaceKHR` 签名改为返回 `SurfaceBundle`
- [x] 5.11 保留 `ExternalYcbcrFormat`；**不引入** `ExternalImageMetadata` / `extractExternalImageMetadata` / `copyExternalImageToMemoryYUV` / `createVkImageFromExternal` / `ImageData`

## 6. 交换链实现

- [x] 6.1 `src/vulkan/platform/VulkanPlatformSwapChainImpl.h`：`VulkanPlatformSwapChainBase`（成员 + 纯虚 `Acquire` / `Present` / `Recreate` / `HasResized` / `IsProtected` + 虚 `QueryCompositorTiming` / `SetPresentFrameId` / `QueryFrameTimestamps` / `Destroy` + 受保护 `CreateImage`）
- [x] 6.2 `VulkanPlatformSurfaceSwapChain`：基于 `VkSurfaceKHR` 的 `vkCreateSwapchainKHR` / `vkAcquireNextImageKHR` / `vkQueuePresentKHR` 实现
- [x] 6.3 `VulkanPlatformHeadlessSwapChain`：不依赖 surface，用 `CreateImage` 直接创建 `VkImage`
- [x] 6.4 **删除全部 `#ifdef __ANDROID__` 分支**，不引入 `AndroidSwapChainHelper.h` / `AndroidNativeWindow.h`
- [x] 6.5 删除上游的 `using namespace bluevk;`（本项目用 `volk`）
- [x] 6.6 `QueryCompositorTiming` / `SetPresentFrameId` / `QueryFrameTimestamps` 返回 `false`
- [x] 6.7 **按上游更正**：交换链内存走 `vkAllocateMemory` / `vkFreeMemory`（不经 VMA），且互换链不持有 `VkImageView`（视图由变更 5 的 `VulkanTexture` 创建）。task 原写的 `vmaFreeMemory` / `VkImageView` 不适用
- [x] 6.8 复核：`SwapChainBundle` 的附件字段与变更 5 构造 `VulkanAttachment` / `VulkanTexture` 的读取点逐字段比对

## 7. VulkanPlatform 交换链分发

- [x] 7.1 **按上游更正**：`createSwapChain` 的 headless 判定依据是 **extent 是否非零**（上游语义），不是 `nativeWindow` 是否为空。测试用 `{1280,720}`，两种规则结果一致
- [x] 7.2 实现 `getSwapChainBundle` / `acquire` / `present` / `recreate` / `hasResized` / `isProtected` / `destroy`，全部转发到 `VulkanPlatformSwapChainBase` 虚接口，**全部对空句柄做防御**
- [x] 7.3 实现 `createSync` / `destroySync`（构造/析构 `Platform::VulkanSync`）
- [x] 7.4 实现 `terminate()`：清理缓存的交换链
- [x] 7.5 把既有实例/设备创建逻辑提取为 `createVkInstance` / `selectVkPhysicalDevice` / `createVkDevice` 三个可覆写钩子的默认实现
- [x] 7.6 复核：提取前后的初始化行为一致（对照既有 `initInstance` / `selectPhysicalDevice` / `createLogicalDevice` 逐行比对）

## 8. VulkanPlatformApple 适配

- [x] 8.1 `include/Backend/platform/VulkanPlatformApple.h`：`createVkSurfaceKHR` 返回类型改为 `SurfaceBundle`
- [x] 8.2 `src/vulkan/platform/VulkanPlatformApple.cpp`：`VK_EXT_metal_surface` 路径保留；查询 `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` 得到 `currentExtent`（`0xFFFFFFFF` 时回退 `minImageExtent`）；`nativeWindow == nullptr` 时返回 `{VK_NULL_HANDLE, {0, 0}}`
- [x] 8.3 复核：`VK_EXT_METAL_SURFACE_EXTENSION_NAME` 仍在 `getSwapchainInstanceExtensions()` 中返回

## 9. 验证

- [x] 9.1 全量构建（`cmake --build build`）无错误、无新增警告
- [x] 9.2 运行 `bin/BackendTests`：8 帧往返正常、无 `LOG_CRITICAL`、exit 0（既有 headless 路径零回归）
- [x] 9.3 `tests/Engine` 新增 headless 交换链往返：`m_platform->createSwapChain(nullptr, 0, {1280, 720})` → `getSwapChainBundle()` → `destroy()`
- [x] 9.4 断言 `SwapChainBundle`：`extent` 非零、`format != VK_FORMAT_UNDEFINED`、`imageCount >= 2`、color 附件非空
- [x] 9.5 断言 `destroy` 后无 VMA 泄漏（`vmaDestroyAllocator` 不触发 `Some allocations were not freed` 断言）
- [x] 9.6 断言 `isCompositorTimingSupported()` 返回 false 且不崩溃
- [x] 9.7 **记录局限**：`acquire` / `present` 在无窗口环境下无法验证（需真实 surface），本变更只验证创建/查询/销毁闭环；SHALL NOT 宣称已端到端验证呈现路径
- [x] 9.8 **记录局限**：`createVkSurfaceKHR` 的 `SurfaceBundle` 返回值在 `nativeWindow != nullptr` 分支无自动化测试（需 `CAMetalLayer`），以代码走查 + 与上游逐行比对为准
- [x] 9.9 记录 headless 交换链在 MoltenVK 上的实测行为（是否需要真实 surface），回填至 design 的 Open Questions

## 实施记录

### 与 spec 的偏离（均已回填 spec）

**核心教训重复了一次**：本 spec 的 `SwapChainBundle` / `ImageSyncData` / `FrameTimestamps` 三处字段是**凭上游调用点推测**的，与 `/Users/turiing/filament` 实际定义不符。实施阶段已逐行对照上游更正，spec 同步修正。这三处与变更 1 的 6 处、变更 2 的字段问题同源。

| # | 偏离 | 理由 |
|---|---|---|
| 1 | 三个结构按上游实际字段实现，而非 spec 的字段枚举 | spec 自身写明「逐字段对齐上游」；推测值有误 |
| 2 | `createSwapChain` 的 headless 判定用 extent 非零 | 上游语义 |
| 3 | 交换链内存用 `vkAllocateMemory`/`vkFreeMemory`，不管理 `VkImageView` | 上游即如此；视图归变更 5 的 `VulkanTexture` |
| 4 | `StereoscopicType` / `GpuContextPriority` 枚举值改 PascalCase，并同步改 8 处引用；新增 `AsynchronousMode` | 项目命名规范（用户决策）；`VulkanPlatform.cpp` 2 处、`VulLogicDevice.{h,cpp}` 6 处一并改掉 |
| 5 | 既有虚函数一并 PascalCase 化（`GetSwapchainInstanceExtensions` 等） | 与新增虚函数保持类内一致 |
| 6 | 三个可覆写钩子真实接线（`VulInstance::Builder::SetInstanceCreator` / `VulLogicDevice::Builder::SetDeviceCreator` / `VulPhysicalDevice::Select`） | 满足 spec「子类可定制」；默认行为不变（`BackendTests` 零回归为证） |
| 7 | `DestroySync` 用 `static_cast<VulkanSync*>` 删除 | 上游裸 `delete Platform::Sync*` 会漏掉成员 `shared_ptr` 的析构（`Platform::Sync` 无虚析构） |
| 8 | `Terminate()` 释放实例/设备包装，不另设交换链缓存 | 本项目无交换链缓存，交换链归驱动所有 |

### 实测结论（回填 design Open Questions）

**headless 交换链在 MoltenVK 上不需要真实 `VkSurfaceKHR`**，创建成功：`extent = 1280x720`、`colorFormat = VK_FORMAT_R8G8B8A8_UNORM`、`imageCount = 2`、`depthFormat = VK_FORMAT_D32_SFLOAT`，销毁后无校验层报错。

### 未验证面

| 项 | 状态 | 兑现变更 |
|---|---|---|
| `Acquire` / `Present` 的真实 surface 路径 | 无自动化测试（需真实 window） | 未规划 |
| `CreateVkSurfaceKHR` 的 `nativeWindow != nullptr` 分支 | 无自动化测试（需 `CAMetalLayer`） | 未规划 |
