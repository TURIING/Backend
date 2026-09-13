## ADDED Requirements

### Requirement: 完整方法清单（143 条）

`DriverAPI.inc` SHALL 从当前 15 条补齐到 143 条，与上游方法集对齐。方法 SHALL 按族分组追加，每组连续放置：

- **帧生命周期**：`setFrameScheduledCallback` / `setFrameCompletedCallback` / `setPresentationTime` / `endFrame`
- **顶点/索引/缓冲**：`createVertexBufferAsync` / `createIndexBufferAsync` / `createBufferObjectAsync` / `updateBufferObject` / `updateBufferObjectAsync` / `updateBufferObjectUnsynchronized` / `updateIndexBuffer` / `updateIndexBufferAsync` / `resetBufferObject` / `setVertexBufferObjectAsync` / `destroyVertexBuffer` / `destroyVertexBufferInfo`
- **纹理**：`createTexture` / `createTextureAsync` / `createTextureView` / `createTextureViewSwizzle` / `createTextureViewSwizzleAsync` / `createTextureExternalImage2` / `createTextureExternalImage` / `createTextureExternalImagePlane` / `importTexture` / `importTextureAsync` / `destroyTexture` / `update3DImage` / `update3DImageAsync` / `generateMipmaps` / `setExternalStream`
- **程序与图元**：`createProgram` / `destroyProgram` / `createRenderPrimitive` / `destroyRenderPrimitive` / `compilePrograms`
- **渲染目标与交换链**：`createDefaultRenderTarget` / `createRenderTarget` / `destroyRenderTarget` / `createSwapChain` / `createSwapChainHeadless` / `destroySwapChain`
- **围栏/同步/计时**：`createSync` / `destroySync` / `getFenceStatus` / `fenceWait` / `fenceCancel` / `getPlatformSync` / `createTimerQuery` / `destroyTimerQuery` / `beginTimerQuery` / `endTimerQuery` / `getTimerQueryValue` / `isCompositorTimingSupported` / `queryCompositorTiming` / `queryFrameTimestamps`
- **描述符集**：`createDescriptorSetLayout` / `createDescriptorSet` / `destroyDescriptorSetLayout` / `destroyDescriptorSet` / `updateDescriptorSetBuffer` / `updateDescriptorSetTexture`
- **内存映射缓冲**：`mapBuffer` / `unmapBuffer` / `copyToMemoryMappedBuffer`
- **绘制路径**：`beginRenderPass` / `endRenderPass` / `nextSubpass` / `bindPipeline` / `bindRenderPrimitive` / `bindDescriptorSet` / `setPushConstant` / `draw` / `draw2` / `drawArrays` / `dispatchCompute` / `scissor` / `makeCurrent` / `commit` / `resolve` / `blit` / `blitDEPRECATED`
- **调试与捕获**：`insertEventMarker` / `pushGroupMarker` / `popGroupMarker` / `startCapture` / `stopCapture` / `queueCommandAsync` / `cancelAsyncJob`
- **读回**：`readPixels` / `readTexture` / `readBufferSubData`
- **流**：`createStreamNative` / `createStreamAcquired` / `setAcquiredImage` / `setStreamDimensions` / `getStreamTimestamp` / `updateStreams` / `destroyStream`
- **特性查询**：`isTextureFormatSupported` / `isTextureSwizzleSupported` / `isTextureFormatMipmappable` / `isTextureFormatFilterable` / `isRenderTargetFormatSupported` / `isFrameBufferFetchSupported` / `isFrameBufferFetchMultiSampleSupported` / `isFrameTimeSupported` / `isAutoDepthResolveSupported` / `isSRGBSwapChainSupported` / `isMSAASwapChainSupported` / `isProtectedContentSupported` / `isProtectedTexturesSupported` / `isStereoSupported` / `isParallelShaderCompileSupported` / `isDepthStencilResolveSupported` / `isDepthStencilBlitSupported` / `isDepthClampSupported` / `isAsynchronousModeEnabled` / `isWorkaroundNeeded` / `getFeatureLevel` / `getClipSpaceParams` / `getMaxDrawBuffers` / `getMaxUniformBufferSize` / `getMaxTextureSize` / `getMaxArrayTextureLayers` / `getUniformBufferOffsetAlignment` / `setupExternalImage2` / `setupExternalImage`

**分族追加约束**：SHALL 按上述族分批追加，每批追加后立即构建通过——`DriverAPI.inc` 的 `PAIR_ARGS_N` / `APPLY_N` 宏展开对参数个数敏感，一次追加 128 条时宏错误难以定位。

**参数个数上限**：`PAIR_ARGS_N` / `APPLY_N` 当前支持到 13 对参数。追加前 SHALL 核对上游是否有超过 13 对的方法（`createTexture` / `createTextureViewSwizzle` / `update3DImage` 接近上限）；若有超出，SHALL 先扩展宏。

#### Scenario: 方法总数

- **WHEN** 统计 `DriverAPI.inc` 的 `DECL_DRIVER_API*` 条目
- **THEN** 为 143 条

#### Scenario: 分族追加可构建

- **WHEN** 追加一个族后执行构建
- **THEN** `CommandStream` 的记录方法与 `ConcreteDispatcher` 的派发表宏展开无误

#### Scenario: 宏参数上限

- **WHEN** 追加 `createTexture`（约 13 对参数）
- **THEN** 宏展开成功；若失败，先扩展 `PAIR_ARGS_N` / `APPLY_N`

### Requirement: 新方法命名规范

新追加的方法 SHALL 使用项目 `PascalCase` 规范（`CreateTexture` / `BeginRenderPass` / `BindPipeline` 等）。

既有方法名 SHALL NOT 修改。`SetVertexBufferObject`（大写 S）与上游 `setVertexBufferObject` 的差异 SHALL 永久保留——用户决策，理由为「对应项目的命名规范」。

**技术依据**：`#define COMMAND_TYPE(method) CommandType<decltype(&Driver::method)>::Command<&Driver::method>`（`src/command/CommandStream.h:69`）从 `&Driver::method` 推导，方法名无需与上游一致。既有 `driver-interface` spec 中「方法名 SHALL 保持 Filament 原版拼写（是 COMMAND_TYPE 机制的前提）」的表述 SHALL 修正——该表述不准确。

#### Scenario: 既有方法名不变

- **WHEN** 检索 `DriverAPI.inc`
- **THEN** `SetVertexBufferObject` 拼写与变更前一致，未被改为小写

#### Scenario: 新方法用 PascalCase

- **WHEN** 检索新追加的方法名
- **THEN** 形如 `CreateTexture` / `BeginRenderPass`，非上游的 `createTexture` / `beginRenderPass`

#### Scenario: 方法名与上游差异不破坏机制

- **WHEN** 编译含 `CommandStream` / `ConcreteDispatcher` 的全部翻译单元
- **THEN** 编译通过，`COMMAND_TYPE` 从 `&Driver::method` 正确推导

## MODIFIED Requirements

### Requirement: 种子方法清单

`DriverAPI.inc` SHALL 保持既有 15 条方法（种子集 8 条 + 缓冲族 7 条）的拼写与签名不变：

- 种子集：`tick` / `beginFrame` / `flush` / `finish` / `resetState` / `createFence` / `destroyFence` / `terminate`
- 缓冲族：`CreateVertexBufferInfo` / `CreateVertexBuffer` / `CreateBufferObject` / `CreateIndexBuffer` / `DestroyBufferObject` / `DestroyIndexBuffer` / `SetVertexBufferObject`

种子集 SHALL 保持上游小写拼写；缓冲族 SHALL 保持项目 `PascalCase` 拼写。两类 SHALL 在文件中按族分段并连续放置，使「上游有而本项目名字不同」的行在人工比对时可见。

原要求中「方法名 SHALL 保持 Filament 原版拼写（与 `Driver` 声明严格一致，是 `COMMAND_TYPE` 机制的前提）」的表述 SHALL 修正为「种子集保持上游拼写仅为对照便利，非 `COMMAND_TYPE` 机制的前提」。

#### Scenario: 种子集与缓冲族不被改动

- **WHEN** 对比本变更前后的 `DriverAPI.inc` 前 15 条
- **THEN** 逐字一致

#### Scenario: 分段标记可见

- **WHEN** 人工比对 `DriverAPI.inc` 与上游
- **THEN** 能通过分段注释区分「名字不同的既有方法」与「新追加的方法」

### Requirement: VulkanDriver 适配

`VulkanDriver` SHALL 改为继承 `DriverBase`（变更 1 建立的中间层），并 override `debugCommandBegin`。

`VulkanDriver` SHALL 实现 `Dispatcher GetDispatcher() const noexcept override`（返回 `ConcreteDispatcher<VulkanDriver>::Make()`）与 `void terminate() override`（转发到 `DestroyResources()`）。

`Create(VulkanPlatform*, const VulkanContextPtr&, const DriverConfig&)` SHALL 保持签名不变，并在构造驱动前把 `DriverConfig::handleArenaSize` 兜底到不小于 8MB。

`DestroyResources()` SHALL 按下列顺序完成（每步判空，保证幂等）：

1. `ResourceManager::Terminate()`
2. 各 Cache 的 `Terminate()`（pipeline / fbo / sampler / ycbcr / descriptor set / descriptor set layout / query）
3. `VulkanCommands::Terminate()`
4. `VulkanBlitter::Terminate()`
5. **`VulkanReadPixels::Terminate()`**
6. `VulkanSemaphoreManager::Terminate()`
7. 各池 `Terminate()` + `Reset()`（bufferCache / stagePool）
8. `vmaDestroyAllocator`

**第 5 步 SHALL 在第 8 步之前**——读回线程若在 `vmaDestroyAllocator` 之后仍运行，会访问已释放的 allocator。

`debugCommandBegin` 的实现 SHALL 使用 `LOG_DEBUG`（本项目已决定不移植 `utils::io::ostream`）。

#### Scenario: VulkanDriver 可编译

- **WHEN** 构建整个 `Backend` 静态库
- **THEN** `src/vulkan/VulkanDriver.cpp` 编译通过，`Create()` 返回 `DriverPtr`

#### Scenario: DestroyResources 幂等

- **WHEN** 连续调用 `terminate()` 两次
- **THEN** 第二次不崩溃，无重复释放

#### Scenario: 读回线程先于 allocator 销毁

- **WHEN** 检查 `DestroyResources()` 的实现顺序
- **THEN** `m_readPixels.Terminate()` 位于 `vmaDestroyAllocator` 之前

#### Scenario: debugCommandBegin 无 ostream 依赖

- **WHEN** 检索 `VulkanDriver.cpp`
- **THEN** 无 `utils::io::ostream` 相关符号
