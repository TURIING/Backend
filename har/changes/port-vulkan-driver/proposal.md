# Change: port-vulkan-driver

## Why

前 6 个变更交付的是**组件**——它们各自可编译、可单元测试，但没有任何一个被驱动层真正调用。本变更把它们全部接进 `VulkanDriver`，补齐 `DriverAPI.inc` 的 128 条方法声明，并把 `VulkanDriver.cpp` 从 160 行扩展到约 2956 行。

**这是唯一产出可观察结果的变更**——变更完成后 `bin/BackendTests` 应能通过 `createSwapChainHeadless` → `createProgram` → `createRenderPrimitive` → `beginRenderPass` → `bindPipeline` → `draw` → `endRenderPass` → `commit` 的完整路径渲染出一个三角形。

## What Changes

### 1. `DriverAPI.inc` 追加 128 条方法

从当前 15 条补齐到 143 条。分族追加：

| 族 | 条数 | 代表方法 |
|---|---|---|
| 帧生命周期 | 4 | `setFrameScheduledCallback` / `setFrameCompletedCallback` / `setPresentationTime` / `endFrame` |
| 顶点/索引/缓冲（异步变体与上传） | 12 | `createVertexBufferAsync` / `createIndexBufferAsync` / `createBufferObjectAsync` / `updateBufferObject(Async)` / `updateIndexBuffer(Async)` / `updateBufferObjectUnsynchronized` / `resetBufferObject` / `setVertexBufferObjectAsync` / `destroyVertexBuffer(Info)` |
| 纹理族 | 17 | `createTexture(Async)` / `createTextureView(Swizzle)(Async)` / `createTextureExternalImage(2/Plane)` / `importTexture(Async)` / `destroyTexture` / `update3DImage(Async)` / `generateMipmaps` / `setExternalStream` |
| 程序与图元 | 6 | `createProgram` / `destroyProgram` / `createRenderPrimitive` / `destroyRenderPrimitive` / `compilePrograms` |
| 渲染目标与交换链 | 8 | `createDefaultRenderTarget` / `createRenderTarget` / `destroyRenderTarget` / `createSwapChain(Headless)` / `destroySwapChain` |
| 围栏/同步/计时 | 12 | `createSync` / `destroySync` / `getFenceStatus` / `fenceWait` / `fenceCancel` / `getPlatformSync` / `createTimerQuery` / `destroyTimerQuery` / `beginTimerQuery` / `endTimerQuery` / `getTimerQueryValue` / `isCompositorTimingSupported` |
| 描述符集 | 6 | `createDescriptorSetLayout` / `createDescriptorSet` / `destroyDescriptorSetLayout` / `destroyDescriptorSet` / `updateDescriptorSetBuffer` / `updateDescriptorSetTexture` |
| 内存映射缓冲 | 3 | `mapBuffer` / `unmapBuffer` / `copyToMemoryMappedBuffer` |
| 绘制路径 | 15 | `beginRenderPass` / `endRenderPass` / `nextSubpass` / `bindPipeline` / `bindRenderPrimitive` / `bindDescriptorSet` / `setPushConstant` / `draw` / `draw2` / `drawArrays` / `dispatchCompute` / `scissor` / `makeCurrent` / `commit` / `resolve` / `blit` |
| 调试与捕获 | 6 | `insertEventMarker` / `pushGroupMarker` / `popGroupMarker` / `startCapture` / `stopCapture` / `queueCommandAsync` / `cancelAsyncJob` |
| 读回 | 3 | `readPixels` / `readTexture` / `readBufferSubData` |
| 流 | 6 | `createStreamNative` / `createStreamAcquired` / `setAcquiredImage` / `setStreamDimensions` / `getStreamTimestamp` / `updateStreams` / `destroyStream` |
| 特性查询 | 25 | `isTextureFormatSupported` / `isRenderTargetFormatSupported` / `getFeatureLevel` / `getClipSpaceParams` / `getMaxDrawBuffers` / `getMaxTextureSize` / `getMaxUniformBufferSize` / `getMaxArrayTextureLayers` / `getUniformBufferOffsetAlignment` / `isWorkaroundNeeded` 等 |

**命名约束**（用户决策）：既有方法名 SHALL NOT 修改。`SetVertexBufferObject` 保持 PascalCase，与上游 `setVertexBufferObject` 永久不同。新追加的方法 SHALL 用项目 `PascalCase` 规范。

### 2. `VulkanDriver` 主体（160 → 约 2956 行）

- **构造**：当前 5 个成员扩展为上游的 17 个成员（`mSemaphoreManager` / `mCommands` / `mPipelineLayoutCache` / `mPipelineCache` / `mFramebufferCache` / `mYcbcrConversionCache` / `mSamplerCache` / `mBlitter` / `mReadPixels` / `mDescriptorSetLayoutCache` / `mDescriptorSetCache` / `mQueryManager` 等）
- **基类**：`Driver` → `DriverBase`（变更 1 建立的中间层），override `debugCommandBegin`
- **帧循环**：`tick` / `beginFrame` / `endFrame` / `flush` / `finish` / `resetState` 的完整实现（当前均为空）
- **绘制路径**：`beginRenderPass` / `endRenderPass` / `bindPipeline` / `bindPipelineImpl` / `bindRenderPrimitive` / `bindDescriptorSet` / `prepareDraw` / `draw2` / `drawArrays` / `draw` / `scissor`
- **资源生命周期**：`DestroyResources` 扩展为销毁全部新组件（含 `mReadPixels` 的线程 join）
- **特性查询**：25 条 `is*Supported` / `get*` 实现
- **新增私有方法**：`PrepareDraw` / `EndCommandRecording` / `AcquireNextSwapchainImage` / `SkipDueToEmptyRenderPass` / `BindPipelineImpl`

**`VulkanDriver.h` 的结构体成员**（上游）：
- `mPipelineState`（含 `program` / `pipelineLayout` / `descriptorSetMask` / `bindInDraw`）
- `mAppState`（`hasExternalSamplerLayouts` / `hasBoundExternalImages`）
- `mRenderPrimitiveState`（`bound`）
- `mCurrentRenderPass`（`VulkanRenderPassContext`）
- `BindInDrawBundle`

### 3. `VulkanDriverFactory.h`

- `CreateVulkanDriver(VulkanPlatform*, VulkanContext&, DriverConfig const&) → Driver*` 工厂函数（供 `VulkanPlatform::CreateDriver` 使用）

### 4. `FeatureFlagManager` 最小形态

`VulkanDriver` 构造期需要 `DriverConfig::featureFlagManager`。变更 3 以 `void const*` 占位，本变更建立最小可用形态：

- `utils::FeatureFlagManager`：`IsEnabled(FeatureFlag)` 查询
- `backend::FeatureFlag` 枚举（若上游 Vulkan 路径实际引用）
- 若上游 Vulkan 路径仅用它判断少数开关，可先提供恒返回默认值的实现

### 5. `VulkanContext` 与 `VulkanPlatform` 的收口

- `VulkanDriver::Create` 签名与 `VulkanPlatform::CreateDriver` 对接
- `MAX_SAMPLER_BINDING_COUNT = Program::SAMPLER_BINDING_COUNT`
- `MAX_RENDERTARGET_ATTACHMENT_TEXTURES` 从变更 5 的 `VulkanConstants.h` 引入

### 6. 三角形验证用例

`tests/` 扩展：构造 headless 交换链 → 编译最小三角形着色器 → 创建图元 → 逐帧 `beginRenderPass` / `bindPipeline` / `draw` / `endRenderPass` / `commit` → `readPixels` 校验中心像素颜色。

## Capabilities

### New Capabilities

- `vulkan-driver`: `VulkanDriver` 主体、构造函数、帧循环、绘制路径、资源生命周期、特性查询、`VulkanDriverFactory`

### Modified Capabilities

- `driver-interface`: `DriverAPI.inc` 从 15 条补齐到 143 条；`VulkanDriver` 继承 `DriverBase`
- `test-engine`: 新增三角形端到端验证用例

## Impact

- 修改：
  - `include/Backend/DriverAPI.inc`（+128 条方法声明）
  - `src/vulkan/VulkanDriver.h` / `VulkanDriver.cpp`（160 → 约 2956 行）
  - `src/vulkan/platform/VulkanPlatform.cpp`（`CreateDriver` 接线）
  - `include/Backend/platform/VulkanPlatform.h`（`DriverConfig` 字段类型收口）
  - `tests/App.cpp` / `Engine.h` / `Engine.cpp`（三角形用例）
- 新增：
  - `src/vulkan/VulkanDriverFactory.h`
  - `src/utils/FeatureFlagManager.h`（或 `src/FeatureFlagManager.h`）
  - `tests/` 下的着色器二进制与验证逻辑
- 不改：前 6 个变更交付的全部组件（`VulkanDriver` 是它们的消费者，不是修改者）
- 验证：`bin/BackendTests` 渲染出三角形且中心像素颜色符合预期

**风险集中点**：
1. `VulkanDriver::DestroyResources` 的销毁顺序（`mReadPixels` 线程须在 `vmaDestroyAllocator` 前 join）
2. 143 条方法中约 25 条特性查询的语义（返回值错误会导致前端选择错误路径）
3. `bindPipelineImpl` 的 20 个 `RasterState` 字段填充（依赖变更 6 的逐字段契约）

**未验证面（须明确记录）**：
- 外部图像路径（`createTextureExternalImage*` / `importTexture*` / `setupExternalImage*`）：用户决策砍掉实现，留空桩
- 视频流路径（`createStream*` / `setAcquiredImage` / `updateStreams`）：同上
- `dispatchCompute`：上游本身即为 `// FIXME: implement me` 空实现，本项目保持一致
- `startCapture` / `stopCapture`：上游空实现
- `queryFrameTimestamps` / `queryCompositorTiming`：MoltenVK 不支持，返回 false
