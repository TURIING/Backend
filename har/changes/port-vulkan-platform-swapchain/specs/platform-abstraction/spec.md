## ADDED Requirements

### Requirement: Platform 句柄族

`Platform` SHALL 定义四个不透明句柄类型：`struct SwapChain {}`、`struct Fence {}`、`struct Stream {}`、`struct Sync {}`。四者 SHALL 为空结构（仅作类型标签），由平台子类派生具体实现。

`Platform` SHALL 定义 `using SyncCallback = void (*)(Sync* sync, void* userData);`。

`Platform` SHALL NOT 定义 `ExternalImage` / `ExternalImageHandle` / `ExternalImageHandleRef`（用户决策：砍掉外部图像路径）。

#### Scenario: 句柄可被平台子类派生

- **WHEN** 声明 `struct VulkanPlatformSwapChainBase : public Platform::SwapChain`
- **THEN** 编译通过，可经 `Platform::SwapChain*` 基类指针持有

#### Scenario: 无外部图像类型

- **WHEN** 检索 `Platform::ExternalImage`
- **THEN** 无该类型定义；`DriverAPI.inc` 中相关方法使用 `void*` 形参，不阻塞声明

### Requirement: 呈现时序结构

`Platform` SHALL 定义：

- `struct CompositorTiming`：`using time_point_ns = int64_t;` / `using duration_ns = int64_t;` / `static constexpr time_point_ns INVALID = -1;` 与四个 `duration_ns` 字段（`compositeInterval` / `compositeDeadlineLatency` / `compositeToPresentLatency` / `expectedPresentLatency`）
- `struct FrameTimestamps`：`time_point_ns` / `duration_ns` / `INVALID = -1` / `PENDING = -2` 与上游实际的时间戳字段集（`requestedPresentTime` / `acquireTime` / `latchTime` 等，**以上游为准确认具体个数与命名**；初稿枚举的 `vsync` / `frameReady` / `frameLatch` 是对上游的推测，与 `/Users/turiing/filament` 实际不符，实施阶段已按上游更正）

两结构 SHALL 逐字段对齐上游（含 `static constexpr` 哨兵值）。

#### Scenario: 哨兵值可区分

- **WHEN** 检查 `FrameTimestamps::INVALID` 与 `FrameTimestamps::PENDING`
- **THEN** 分别为 -1 与 -2，不相等于任何合法时间戳

### Requirement: Platform 呈现时序虚函数

`Platform` SHALL 提供以下虚函数，均带默认实现（子类可不覆写）：

- `virtual bool isCompositorTimingSupported() const noexcept`：默认返回 `false`
- `virtual bool queryCompositorTiming(SwapChain const* swapchain, CompositorTiming* outCompositorTiming) const noexcept`：默认返回 `false`
- `virtual bool setPresentFrameId(SwapChain const* swapchain, uint64_t frameId) const noexcept`：默认返回 `false`
- `virtual bool queryFrameTimestamps(SwapChain const* swapchain, uint64_t frameId, FrameTimestamps* outFrameTimestamps) const noexcept`：默认返回 `false`
- `virtual Sync* createSync(std::shared_ptr<VulkanCmdFence> fenceStatus) noexcept`：默认返回 `nullptr`
- `virtual void destroySync(Sync* sync) noexcept`：默认空实现
- `virtual bool pumpEvents() noexcept`：默认返回 `false`

`VulkanCmdFence` SHALL 以 `struct VulkanCmdFence;` 前置声明引入，`Platform.h` SHALL NOT include `vulkan/sync/VulkanCmdFence.h`——该头位于 `src/`（私有层），公共头反向依赖 `src/` 会破坏分层。

**范围限定**：MoltenVK 不支持 `VK_GOOGLE_display_timing`，本项目的 `VulkanPlatform` SHALL 不覆写前四个函数，全部走默认 `false`。结构完整移植的理由是它们是 `DriverAPI.inc` 两条方法的签名契约，砍掉会导致 143 方法无法全量对齐。

#### Scenario: 默认返回 false 不崩溃

- **WHEN** 在未覆写的 `VulkanPlatform` 上调用 `isCompositorTimingSupported()` 与 `queryFrameTimestamps(...)`
- **THEN** 分别返回 false，输出参数不被写入，无崩溃

#### Scenario: Platform.h 不依赖 src/

- **WHEN** include `Backend/platform/Platform.h`
- **THEN** 不引入 `src/` 下的任何头文件（`VulkanCmdFence` 为前置声明）

### Requirement: DriverConfig 全字段

`Platform::DriverConfig` SHALL 包含以下字段（逐一对齐上游）：

- `FeatureFlagManager const* featureFlagManager = nullptr`（可空）

  **注**：`FeatureFlagManager` 的完整实现在本变更**不引入**；若该类型尚不存在，本字段以 `void const*` 占位并在变更 7 建立 `FeatureFlagManager` 时改为强类型。此处置须在 tasks 中记录。

- `size_t handleArenaSize = 0`
- `size_t metalUploadBufferSizeBytes = 512 * 1024`（保留字段，Vulkan 路径不消费）
- `bool disableParallelShaderCompile = false`
- `bool disableAmortizedShaderCompile = true`
- `bool disableHandleUseAfterFreeCheck = false`
- `bool disableHeapHandleTags = false`
- `bool forceGLES2Context = false`
- `StereoscopicType stereoscopicType = StereoscopicType::NONE`
- `AsynchronousMode asynchronousMode = AsynchronousMode::NONE`
- `GpuContextPriority gpuContextPriority = GpuContextPriority::DEFAULT`

**权威位置决策**：`StereoscopicType` / `GpuContextPriority` / `AsynchronousMode` 三个枚举的**权威定义 SHALL 落在 `Platform.h`**；`DriverDefine.h` SHALL 以 `using` 别名引用（`using StereoscopicType = Platform::StereoscopicType;`）。

理由：上游的权威位置在 `Platform.h`，且 `DriverConfig`（属于 `Platform`）需要它们；若反过来由 `DriverDefine.h` 定义，会形成 `Platform.h` ← `DriverDefine.h` ← `Platform.h` 的循环 include。

**循环 include 处置**：`Platform.h` 当前 `#include "Backend/Driver.h"`（仅为拿 `DriverPtr`）。本变更 SHALL 改为 `DECLARE_CLASS_AND_SHARE_PTR(Driver)` + 前置声明，使 `Platform.h` 不再 include `Driver.h`。

#### Scenario: 默认值对齐上游

- **WHEN** 默认构造 `DriverConfig{}`
- **THEN** `handleArenaSize == 0`、`disableAmortizedShaderCompile == true`、`stereoscopicType == StereoscopicType::NONE`、`disableHandleUseAfterFreeCheck == false`

#### Scenario: 无循环 include

- **WHEN** 单独 include `Backend/platform/Platform.h`
- **THEN** 编译通过，不依赖 `Backend/Driver.h`

#### Scenario: 别名解析

- **WHEN** 在 `DriverDefine.h` 写 `using StereoscopicType = Platform::StereoscopicType;` 并 include `Backend/platform/Platform.h`
- **THEN** `Backend::StereoscopicType` 解析到 `Platform::StereoscopicType`

## MODIFIED Requirements

### Requirement: 平台抽象基类

`Platform` SHALL 保持继承 `utils::Ref` 并保留 `DECLARE_CLASS_AND_SHARE_PTR(Platform)`（`PlatformPtr`），SHALL 提供虚析构。

`Platform` SHALL 保留纯虚 `DriverPtr CreateDriver(const DriverConfig& config, void* shareContext)`。

`Platform` SHALL NOT include `Backend/Driver.h`；`DriverPtr` 经 `DECLARE_CLASS_AND_SHARE_PTR(Driver)` 获取（该方法返回 `DriverPtr`，只需该别名可见）。

#### Scenario: 子类仍可实现 CreateDriver

- **WHEN** `VulkanPlatform` 实现 `CreateDriver(const DriverConfig&, void*)`
- **THEN** 编译通过，返回 `DriverPtr`

#### Scenario: 头文件自足

- **WHEN** 单独 include `Backend/platform/Platform.h` 并声明 `PlatformPtr p;`
- **THEN** 编译通过

## REMOVED Requirements

（无。原 `platform-abstraction` spec 只覆盖 `Utils/ashmem.h` 与页大小工具，本变更不改动那些需求；`Platform` 类此前未被该 spec 覆盖，故本变更为纯 ADDED + 对既有 `Platform` 类声明的首次规格化。）
