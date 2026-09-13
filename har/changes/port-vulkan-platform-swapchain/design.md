# Design: port-vulkan-platform-swapchain

## Context

本变更把平台层从「只能创建设备」扩展到「能创建与驱动交换链」。它是变更 5（`VulkanSwapChain`）的硬前置。

上游的分层：

```
  backend/include/backend/Platform.h              抽象句柄 + 时序结构 + 7 个虚函数
        │                                          SwapChain / Fence / Stream / Sync
        ▼
  backend/include/backend/platforms/VulkanPlatform.h
        │                                          SwapChainBundle / ImageSyncData /
        │                                          createSwapChain / acquire / present
        ▼
  backend/src/vulkan/platform/VulkanPlatformSwapChainImpl.{h,cpp}
        │                                          VulkanPlatformSwapChainBase
        │                                          ├─ VulkanPlatformSurfaceSwapChain
        │                                          └─ VulkanPlatformHeadlessSwapChain
        ▼
  backend/src/vulkan/platform/VulkanPlatform{Apple,Android,Linux,Windows}.cpp
                                                  平台特定的 createVkSurfaceKHR
```

本项目现状与上游的差异：

| 项 | 本项目 | 上游 |
|---|---|---|
| `Platform` 行数 | 13 行，1 个纯虚 | 约 480 行，含 4 句柄 + 2 时序结构 + 7 虚函数 |
| `VulkanPlatform.h` 位置 | `include/Backend/platform/` | `include/backend/platforms/` |
| `createVkSurfaceKHR` 返回 | `VkSurfaceKHR` | `SurfaceBundle`（`tuple<VkSurfaceKHR, VkExtent2D>`） |
| 平台实现文件 | `VulkanPlatformApple.cpp`（32 行） | `.mm`（Objective-C++） |
| 交换链实现 | 无 | `VulkanPlatformSwapChainImpl`（662 行，含 Android 分支） |

关键事实：**`VulkanPlatformSwapChainImpl.cpp` 不含任何 Apple 专属代码**——它统一走 `VkSurfaceKHR`，平台差异全部由 `createVkSurfaceKHR` 承担（Apple 用 `VK_EXT_metal_surface`）。因此「只做 macOS」的实际含义是「跳过全部 `#ifdef __ANDROID__` 分支」，而非另写一套实现。

## Goals / Non-Goals

**Goals:**

- `Platform` 具备承载交换链的最小完整抽象（句柄族 + 时序结构 + 虚函数）
- `VulkanPlatform` 具备 `createSwapChain` / `acquire` / `present` / `recreate` 完整接口
- 两种交换链实现可创建与销毁（surface 与 headless）
- `createVkSurfaceKHR` 签名与上游对齐（返回 `SurfaceBundle`）
- `bin/BackendTests` 的既有 headless 路径不回归

**Non-Goals:**

- 不移植 `VulkanSwapChain`（变更 5）——本变更只到平台层，`VulkanDriver` 尚不调用 `createSwapChain`
- 不移植 `ExternalImage` / `ExternalImageHandle` / `ExternalImageMetadata` / `copyExternalImageToMemoryYUV` / `createVkImageFromExternal` / `ImageData`（用户决策：砍掉外部图像）
- 不移植 `#ifdef __ANDROID__` 分支（用户决策：只做 macOS / MoltenVK）
- 不移植 `AndroidSwapChainHelper` / `AndroidNativeWindow`
- 不实现 `queryFrameTimestamps` / `queryCompositorTiming` 的真实时序查询（MoltenVK 无对应扩展，返回 false 即可）
- 不引入窗口系统集成（`Window` / `CAMetalLayer` 的创建由 app 负责，后端只接收 `void* nativeWindow`）

## Decisions

### D1: `createVkSurfaceKHR` 改为返回 `SurfaceBundle`

上游签名：

```cpp
using SurfaceBundle = std::tuple<VkSurfaceKHR, VkExtent2D>;
virtual SurfaceBundle createVkSurfaceKHR(void* nativeWindow, VkInstance instance,
        uint64_t flags) const noexcept;
```

本项目当前返回裸 `VkSurfaceKHR`。

**决策**：对齐上游，改为返回 `SurfaceBundle`。

**理由**：`VkExtent2D` 是 headless 场景的必要信息——不传 native window 时驱动需要一个虚拟尺寸来创建 swapchain。当前项目无调用方（`createSwapChainR` 未移植），改动成本为零；若保持裸 `VkSurfaceKHR`，变更 5 落地时还要回头改这里。

**代价**：`.cpp` 实现返回值改 `std::make_tuple(surface, extent)`；调用方（变更 5）解构。

### D2: `Platform` 扩展只做「有消费者」的部分

上游 `Platform` 还包含 `ExternalImage` / `ExternalImageHandle`（引用计数句柄，约 80 行）。用户决策砍掉外部图像，故：

**决策**：不移植 `ExternalImage` / `ExternalImageHandle` / `ExternalImageHandleRef`。`DriverAPI.inc` 中 `setAcquiredImage` / `setupExternalImage` / `setupExternalImage2` / `createStreamNative` 等方法的签名里出现 `void* image`（而非 `ExternalImageHandleRef`）——上游这两处本就用 `void*`，故不阻塞。

**核对项**：`include/Backend/platform/Platform.h` 扩展后，`DriverAPI.inc` 的全部 143 条声明中引用的每一个 `Platform::` 类型都必须可解析。tasks 中单列一条核对。

### D3: `CompositorTiming` / `FrameTimestamps` 完整移植但实现返回 false

两个结构各约 40 行，是 `queryCompositorTiming` / `queryFrameTimestamps` 的返回类型。MoltenVK 不支持 `VK_GOOGLE_display_timing`，`isCompositorTimingSupported()` 返回 false。

**决策**：结构完整移植（`DriverAPI.inc` 签名需要），实现返回 `false` + 输出参数置 `INVALID`。不引入扩展探测。

**理由**：结构与实现可以分离——结构是签名契约，实现是能力声明。砍掉结构会导致 2 条驱动方法无法声明，破坏「143 方法全量对齐」的目标。

### D4: 交换链实现只保留 Surface 与 Headless 两条路径

上游 `VulkanPlatformSwapChainImpl.h` 定义：

```
VulkanPlatformSwapChainBase（抽象基类，662 行 cpp 中的公共部分）
├─ VulkanPlatformSurfaceSwapChain   基于 VkSurfaceKHR
└─ VulkanPlatformHeadlessSwapChain  基于虚拟 extent
```

Android 的 `VulkanPlatformSurfaceSwapChain` 内有 `#ifdef __ANDROID__` 分支（`ANativeWindow` 相关，第 166/362/380/388 行）。

**决策**：保留两个类，**删除所有 `#ifdef __ANDROID__` 分支**（不留空 `#ifdef` 骨架）。理由——留下永不编译的死代码违反 `.dsh/rules/code-style.md`「注释掉的代码（删除而非注释）」的同理精神，且未来若做 Android 应重写而非复活。

**Headless 的用途**：`bin/BackendTests` 在 macOS 上无窗口，`createSwapChainHeadless` 是唯一可测路径。本变更的验证正落在它上面。

### D5: `VulkanSync` 的平台侧与驱动侧是两个类型

上游存在两个同名类型：

```
Platform::VulkanSync（定义于 platforms/VulkanPlatform.h:484）
    struct VulkanSync : public Platform::Sync {
        std::shared_ptr<VulkanCmdFence> fenceStatus;
    };
    ← 平台层：承载共享围栏，供 createSync/destroySync 使用

Backend::VulkanSync（定义于 VulkanAsyncHandles.h）
    struct VulkanSync : ThreadSafeResource, public HwSync {
        utils::Mutex lock;
        std::vector<std::unique_ptr<CallbackData>> conversionCallbacks;
    };
    ← 驱动层：HwSync 的资源对象，内含 Platform::Sync*
```

**决策**：两者都移植，但归属不同变更——平台侧 `VulkanSync` 在本变更（随 `VulkanPlatform::createSync` 一起），驱动侧 `VulkanSync` 在变更 6（随 `VulkanAsyncHandles`）。

**命名冲突处理**：平台侧为 `Platform::VulkanSync`（嵌套），驱动侧为 `Backend::VulkanSync`（命名空间级）。项目 `Backend` 命名空间下引用 `VulkanSync` 会解析到驱动侧；引用平台侧须写 `Platform::VulkanSync`。此规则须在 tasks 中记录，避免变更 6 误用。

### D6: `VulkanPlatform.cpp` 的增量移植范围

上游 `VulkanPlatform.cpp` 1183 行，本项目 515 行（已移植实例/设备/队列/特性查询）。本变更追加的部分：

- `createSwapChain` / `getSwapChainBundle` / `acquire` / `present` / `recreate` / `hasResized` / `isProtected` / `destroy`：转发到 `VulkanPlatformSwapChainBase` 虚接口
- `createSync` / `destroySync`：构造/析构 `Platform::VulkanSync`
- `terminate()`：清理缓存的交换链
- `createVkInstance` / `selectVkPhysicalDevice` / `createVkDevice`：提取既有内联逻辑为可覆写钩子

**不追加**：`extractExternalImageMetadata` / `copyExternalImageToMemoryYUV` / `createVkImageFromExternal`（外部图像，已砍）。

### D7: 验证策略——headless 交换链闭环

本变更没有可呈现的画面（`VulkanDriver` 尚不接线），可验证的是平台层的交换链生命周期：

1. **全量构建**无错误
2. **`bin/BackendTests` 不回归**（既有 8 帧往返仍 exit 0）
3. **新增 headless 交换链往返测试**：在 `tests/Engine` 中直接调用 `m_platform->createSwapChain(nullptr, flags)` → `getSwapChainBundle()` → `destroy()`，验证：
   - 返回非空 `SwapChainPtr`
   - `SwapChainBundle` 的 `extent` 非零、`format` 非 `VK_FORMAT_UNDEFINED`、`imageCount >= 2`
   - `destroy` 后无 VMA / Vulkan 校验层报错
4. **`isCompositorTimingSupported()` 返回 false** 且不崩溃

**固有局限**：`acquire` / `present` 无法在无窗口环境下验证（需要真实 surface）。tasks 须明确记录，不得宣称已端到端验证呈现路径。

## Risks / Trade-offs

- [`createVkSurfaceKHR` 签名 BREAKING] → 当前零调用方（`createSwapChainR` 未移植），实际影响为零；变更 5 按新签名接线
- [`Platform` 扩展后与 `DriverDefine.h` 的类型别名环] → `DriverDefine.h` 定义 `using StereoscopicType = Platform::StereoscopicType;` 而 `Platform.h` 又 include `Driver.h`（含 `DriverDefine.h`），存在循环 include 风险。**处置**：`StereoscopicType` / `GpuContextPriority` / `AsynchronousMode` 的权威定义落在 `Platform.h`，`DriverDefine.h` 只做 `using` 别名；`Platform.h` 改为前置声明所需类型而非 include `Driver.h`（仅需 `DriverPtr`，可用 `DECLARE_CLASS_AND_SHARE_PTR`）
- [删除 Android 分支导致未来不支持 Android] → 有意取舍（用户决策：只做 macOS）；若将来要支持，按上游重写而非复活死代码
- [`Platform::VulkanSync` 与 `Backend::VulkanSync` 同名] → D5 记录限定名的使用规则；变更 6 落地时须复核
- [headless 交换链在 MoltenVK 上的行为未验证] → `bin/BackendTests` 当前完全没走交换链路径；本变更的往返测试是首次触达，若 MoltenVK 对 headless swapchain 有额外约束（如需 `VK_KHR_surface` 扩展但无 surface），须在实施中记录并调整
- [`queryCompositorTiming` 返回 false 但结构完整移植] → 结构与实现分离（D3）；无功能风险，仅增加约 80 行无消费的实现

## Open Questions

- headless 交换链在 MoltenVK 上是否需要真实 `VkSurfaceKHR`：上游 `VulkanPlatformHeadlessSwapChain` 通过 `createImage` 直接创建 `VkImage`（不经过 surface），理论上不需要 surface；本变更的往返测试会给出答案
  - **实施结论（已实测）**：不需要 surface。`bin/BackendTests` 在无窗口环境下 `CreateSwapChain(nullptr, 0, {1280, 720})` 创建成功，bundle 为 `extent = 1280x720`、`colorFormat = VK_FORMAT_R8G8B8A8_UNORM`、`imageCount = 2`、`depthFormat = VK_FORMAT_D32_SFLOAT`；`Destroy()` 后进程 exit 0，无校验层与内存泄漏报错
- `pumpEvents()` 是否本变更需要：上游 `Platform::pumpEvents` 默认返回 `false`，仅 Android 覆写。本变更可不引入（无调用方），留到变更 7 若 `VulkanDriver` 需要时补
- `getFenceExportFlags()` / `VkExternalFenceHandleTypeFlagBits` 是否保留：`VulkanContext` 已有 `m_fenceExportFlags`，属外部同步语义。当前已在本项目中存在，保持不动；若与砍掉的外部图像路径有关联，在变更 7 收口
- 平台实现文件是否应改为 `.mm`：上游 Apple 实现为 `.mm`（Objective-C++），本项目现有为 `.cpp`（`CAMetalLayer` 经前置声明为不透明类型）。当前 `createVkSurfaceKHR` 能编译，本变更不改扩展名；若后续需要访问 `CAMetalLayer` 的属性（如 `drawableSize`）则须改 `.mm`，并需在 CMake 中为该文件单独设置 `LANGUAGE OBJCXX`
