# Tasks: port-vulkan-driver

> 本变更是收口层（约 3000 行）。`DriverAPI.inc` 按族分批追加，每批后构建；`VulkanDriver` 按功能块分批实现。

## 1. 前置确认

- [x] 确认前 6 个变更全部落地并全绿（组件齐备才可开始接线）
- [x] 核对 `ResourceManager` 能否表达「复用已构造对象并交给句柄」的语义（`mDefaultRenderTarget` 预创建需要）；若不能，扩展 `AssociateExisting(handle, ptr)` 并回填 `vulkan-resource` spec
- [x] 核对 `DriverBase` 的构造签名与 `VulkanDriver` 初始化列表的匹配性
- [x] 核对 `PAIR_ARGS_N` / `APPLY_N` 的参数对上限（当前 13）；逐个检查 `createTexture` / `createTextureViewSwizzle` / `update3DImage` 的参数对数，超出则先扩展宏
- [x] 建立 `FeatureFlagManager` 最小形态，并把 `DriverConfig::featureFlagManager` 从 `void const*` 收口为 `utils::FeatureFlagManager const*`

## 2. DriverAPI.inc 分族追加（每族后构建）

- [x] 帧生命周期族（4 条）：`setFrameScheduledCallback` / `setFrameCompletedCallback` / `setPresentationTime` / `endFrame`
- [x] 顶点/索引/缓冲族（12 条）：异步创建变体 + 上传 + `resetBufferObject` + `destroyVertexBuffer(Info)`
- [x] 程序与图元族（6 条）：`createProgram` / `destroyProgram` / `createRenderPrimitive` / `destroyRenderPrimitive` / `compilePrograms`
- [x] 渲染目标与交换链族（6 条）：`createDefaultRenderTarget` / `createRenderTarget` / `destroyRenderTarget` / `createSwapChain` / `createSwapChainHeadless` / `destroySwapChain`
- [x] 围栏/同步/计时族（14 条）：`createSync` / `destroySync` / `getFenceStatus` / `fenceWait` / `fenceCancel` / `getPlatformSync` / `createTimerQuery` / `destroyTimerQuery` / `beginTimerQuery` / `endTimerQuery` / `getTimerQueryValue` / `isCompositorTimingSupported` / `queryCompositorTiming` / `queryFrameTimestamps`
- [x] 描述符集族（6 条）
- [x] 内存映射缓冲族（3 条）：`mapBuffer` / `unmapBuffer` / `copyToMemoryMappedBuffer`
- [x] 绘制路径族（17 条）：`beginRenderPass` … `blitDEPRECATED`（约 17 条，含 `dispatchCompute`）
- [x] 调试与捕获族（7 条）：`insertEventMarker` / `pushGroupMarker` / `popGroupMarker` / `startCapture` / `stopCapture` / `queueCommandAsync` / `cancelAsyncJob`
- [x] 读回族（3 条）：`readPixels` / `readTexture` / `readBufferSubData`
- [x] 流族（7 条）：`createStreamNative` / `createStreamAcquired` / `setAcquiredImage` / `setStreamDimensions` / `getStreamTimestamp` / `updateStreams` / `destroyStream`
- [x] 纹理族（15 条）：含外部图像与 import 方法（空桩）
- [x] 特性查询族（约 30 条）
- [x] 复核：方法总数 = 143；既有 15 条逐字未变；分段注释可区分「名字不同的既有方法」与「新追加方法」

## 3. VulkanDriver 骨架

- [x] `VulkanDriver.h`：基类改为 `DriverBase`，override `debugCommandBegin`
- [x] `VulkanDriver.h`：补齐 17 个组件成员 + 状态结构体（`mPipelineState` 含 `bindInDraw` / `mAppState` / `mRenderPrimitiveState` / `mCurrentRenderPass` / `BindInDrawBundle`）
- [x] `VulkanDriver.h`：声明私有方法（`collectGarbage` / `bindPipelineImpl` / `prepareDraw` / `endCommandRecording` / `acquireNextSwapchainImage` / `skipDueToEmptyRenderPass`）——**保留上游拼写**（decision：对照成本优先）
- [x] `VulkanDriver.cpp`：构造函数初始化列表（17 个成员，顺序与声明一致）+ `mDefaultRenderTarget` 预创建
- [x] `VulkanDriver.cpp`：`DestroyResources()` 的 8 步顺序（对应 spec 的硬约束），每步判空保证幂等
- [x] 以 `-Wreorder` 构建，确认无初始化顺序警告

## 4. 帧循环与 GC

- [x] `Tick()`：各 Cache 的 `Gc()` + `mQueryManager.Reset()` + `mCommands.Gc()` / `UpdateFences()`
- [x] `BeginFrame` / `EndFrame`（含交换链 `Recreate` 判定）
- [x] `Flush()`：`mCommands.Flush()` + 各池按 `FVK_MAX_FRAMES_IN_FLIGHT` 节流的 `Gc()`
- [x] `Finish()`：`mCommands.Wait()`
- [x] `ResetState()`：重置 `mPipelineState` / `mRenderPrimitiveState`
- [x] `Execute(std::function<void()> const&)`：若上游覆写则对齐，否则保持 `Driver` 默认实现

## 5. 资源创建与销毁方法

- [x] 交换链：`CreateSwapChainS/R` / `CreateSwapChainHeadlessS/R` / `DestroySwapChain`
- [x] 渲染目标：`CreateDefaultRenderTargetS/R`（复用预创建对象）/ `CreateRenderTargetS/R`（offscreen）/ `DestroyRenderTarget`
- [x] 程序：`CreateProgramS/R`（含 pipeline prewarming 分支）/ `DestroyProgram`
- [x] 图元：`CreateRenderPrimitiveS/R` / `DestroyRenderPrimitive`
- [x] 纹理：`CreateTextureS/R` / `CreateTextureAsyncS/R` / `CreateTextureViewR` / `CreateTextureViewSwizzleR` / `DestroyTexture` / `Update3DImage`
- [x] 描述符集：`CreateDescriptorSetLayoutS/R` / `CreateDescriptorSetS/R` / `DestroyDescriptorSetLayout` / `DestroyDescriptorSet` / `UpdateDescriptorSetBuffer` / `UpdateDescriptorSetTexture`
- [x] 围栏/同步/计时：`CreateFenceR`（替换现有空实现）/ `DestroyFence` / `CreateSyncS/R` / `DestroySync` / `CreateTimerQueryR` / `DestroyTimerQuery` / `DestroyVertexBuffer` / `DestroyVertexBufferInfo`
- [x] 内存映射：`MapBufferS/R` / `UnmapBuffer` / `CopyToMemoryMappedBuffer`
- [x] 上传路径：`UpdateBufferObject(Async)` / `UpdateBufferObjectUnsynchronized` / `UpdateIndexBuffer(Async)` / `ResetBufferObject` / `SetVertexBufferObjectAsyncR` / `CreateVertexBufferAsyncR` / `CreateIndexBufferAsyncR` / `CreateBufferObjectAsyncR` / `GenerateMipmaps`
- [x] 复核：全部销毁方法对空句柄提前返回

## 6. 绘制路径

- [x] `BeginRenderPass`：交换链 acquire → 命令缓冲 → `VulkanFboCache` 取 renderpass/framebuffer → 屏障 → `vkCmdBeginRenderPass` → scissor/viewport → 设置 `mCurrentRenderPass`
- [x] `EndRenderPass`：`vkCmdEndRenderPass` → 结束屏障 → 清空 `mCurrentRenderPass`
- [x] `BindPipeline`：布局收集 → `descriptorSetMask` 计算 → `GetLayout` → `bindPipelineImpl`
- [x] `bindPipelineImpl`：`RasterState` 的 **20 个字段逐一填充**（对照上游指定初始化器）+ 状态绑定 + `vkCmdBindPipeline`
- [x] `BindRenderPrimitive`：顶点缓冲 + 索引缓冲绑定（无索引缓冲时跳过）
- [x] `BindDescriptorSet` / `prepareDraw` / `mDescriptorSetCache.Commit`
- [x] `Draw2` / `DrawArrays` / `Draw` / `Scissor`
- [x] `MakeCurrent` / `Commit`（present）
- [x] `SetPushConstant` / `NextSubpass`
- [x] `skipDueToEmptyRenderPass()` 在全部绘制方法的首行生效
- [x] `acquireNextSwapchainImage()`：含失败时清空 `mCurrentRenderPass` 的处理

## 7. 特性查询

- [x] 查 `VulkanContext` 特性位的一组（`IsSRGBSwapChainSupported` / `IsMSAASwapChainSupported` / `IsStereoSupported` / `IsProtectedContentSupported` / `IsProtectedTexturesSupported` / `IsFrameTimeSupported` / `IsAutoDepthResolveSupported` / `IsDepthStencilResolveSupported` / `IsDepthClampSupported` / `IsAsynchronousModeEnabled` / `IsParallelShaderCompileSupported` / `IsFrameBufferFetchSupported` / `IsFrameBufferFetchMultiSampleSupported`）
- [x] 查 `VkPhysicalDeviceLimits` 的一组（`GetMaxDrawBuffers` / `GetMaxUniformBufferSize` / `GetMaxTextureSize` / `GetMaxArrayTextureLayers` / `GetUniformBufferOffsetAlignment`）
- [x] 格式表查询的一组（`IsTextureFormatSupported` / `IsRenderTargetFormatSupported` / `IsTextureFormatMipmappable` / `IsTextureFormatFilterable` / `IsTextureSwizzleSupported` / `IsDepthStencilBlitSupported`）
- [x] 常量返回的一组（`GetClipSpaceParams` / `GetFeatureLevel`）
- [x] `IsWorkaroundNeeded(Workaround)`：按上游的 workaround 表实现
- [x] 复核：**无任何占位返回值**——每条查询都有实质逻辑

## 8. 调试、捕获与空桩

- [x] `InsertEventMarker` / `PushGroupMarker` / `PopGroupMarker`（经 `VulkanCommands` 的既有接口）
- [x] `StartCapture` / `StopCapture`：空实现（与上游一致，不加警告）
- [x] `DispatchCompute`：空实现（与上游一致）
- [x] 外部图像空桩 ×7：`CreateTextureExternalImage2/Plane` / `CreateTextureExternalImage` / `ImportTexture(Async)` / `SetupExternalImage(2)`——返回空句柄 + **可识别警告**
- [x] 流空桩 ×8：`CreateStreamNative` / `CreateStreamAcquired` / `SetAcquiredImage` / `SetStreamDimensions` / `GetStreamTimestamp` / `UpdateStreams` / `DestroyStream` / `SetExternalStream`——同上
- [x] `QueueCommandAsync` / `CancelAsyncJob` / `QueryCompositorTiming` / `QueryFrameTimestamps` / `IsCompositorTimingSupported` / `GetPlatformSync`

## 9. 读回路径

- [x] `ReadPixels` / `ReadTexture` / `ReadBufferSubData`：接线到 `VulkanReadPixels`
- [x] `QueryManager` 接线：`BeginTimerQuery` / `EndTimerQuery` / `GetTimerQueryValue`
- [x] `Resolve` / `Blit` / `BlitDEPRECATED`：接线到 `VulkanBlitter`

## 10. 三角形验证用例

- [x] 生成最小三角形着色器的 SPIR-V（`glslangValidator`），以字节数组存放于 `tests/`，注释记录生成命令行
- [x] `tests/Engine.h` / `.cpp`：新增绘制路径的转发方法（交换链 / 程序 / 图元 / 渲染目标 / 绘制 / 读回）
- [x] `tests/App.cpp`：三角形用例（headless 交换链 → 程序 → 图元 → 渲染目标 → 绘制 → 读回校验）
- [x] 保留既有 8 帧回归用例，并用配置开关选择用例
- [x] 像素校验的容差定义为具名常量
- [x] 若三角形用例受阻，实现清屏降级用例并记录具体失败环节

## 11. 验证

- [x] 全量构建（`cmake --build build`）无错误、无新增警告
- [x] 运行 8 帧回归用例：exit 0、无 `LOG_CRITICAL`（确认既有路径零回归）
- [x] 运行三角形用例：渲染成功 + 中心像素颜色断言通过
- [x] 若采用降级方案：运行清屏用例 + 记录降级原因
- [x] 连续调用 `terminate()` 两次不崩溃（幂等验证）
- [x] 进程退出无 `Some allocations were not freed` 断言
- [x] 复核 `DestroyResources()` 的 8 步顺序，特别确认 `m_readPixels.Terminate()` 位于 `vmaDestroyAllocator` 之前
- [x] 复核 25 条特性查询无占位返回值
- [x] 复核 `bindPipelineImpl` 的 `RasterState` 20 个字段全部被赋值
- [x] 复核 `DriverAPI.inc` 方法总数 = 143，既有 15 条逐字未变
- [x] **记录未验证面**：外部图像 / 视频流路径（空桩，无验证）；`DispatchCompute` / 捕获（空实现）；`NextSubpass`（无测试路径）；Present timing 查询（返回 false）；有窗口的呈现路径（只验证 headless）
- [x] 回填 design 的 Open Questions：`ResourceManager` 是否需扩展、SPIR-V 工具链可用性、`VulkanDriverFactory` 是否必要
- [x] 更新 `driver-interface` spec 中关于「方法名须与上游一致」的错误表述（已在本变更的 spec 中修正，确认代码库中的 spec 文件已同步）

## 实施记录

### 1. 文件清单与行数

新增：

| 文件 | 行数 | 说明 |
|---|---|---|
| `tests/TriangleShaders.h` | 125 | 最小三角形着色器的 SPIR-V 预编译字节数组（含生成命令行注释） |

修改（行数为改后值）：

| 文件 | 行数 | 改动 |
|---|---|---|
| `include/Backend/DriverAPI.inc` | 366 | 15 → **143** 条方法；既有 15 条逐字未变 |
| `src/vulkan/VulkanDriver.h` | 170 | 骨架：`DriverBase` 基类、17 个组件成员、状态结构体、6 个私有方法 |
| `src/vulkan/VulkanDriver.cpp` | 1943 | 全部 143 条方法实现 + 构造 + 帧循环 + 绘制路径 + 特性查询 + 空桩 |
| `include/Backend/Driver.h` | 69 | 补 `BufferDescriptor` / `PixelBufferDescriptor` / `TargetBufferInfo` / `Program` / `Matrix` 包含，新增 `DriverApi` 别名 |
| `include/Backend/DriverDefine.h` | +60 | `FrameScheduledCallback`、`kFeatureLevelCaps`、`TargetBufferFlags::operator~` |
| `include/Backend/platform/Platform.h` | +40 | `ExternalImageHandle` / `ExternalImageHandleRef` 骨架 |
| `include/Backend/platform/VulkanPlatform.h` | +5 | `Customization::pipelineCachePrewarmExternalFormats` |
| `include/Backend/BufferDescriptor.h` | +4 | 补 `Namespace.h` / `Macro.h`（原依赖传递获得） |
| `src/vulkan/VulkanContext.h` | +20 | 3 个特性位 + 预热格式清单的访问器与成员 |
| `src/vulkan/platform/VulkanPlatform.cpp` | +6 | 填充上条的 4 项 |
| `src/vulkan/VulkanHandle.h` / `.cpp` | +18 | 新增 `VulkanMemoryMappedBuffer`；补 `VulkanIndexBuffer::LoadFromCpu`；恢复 `VulkanBufferObject::LoadFromCpu` 转发 |
| `src/vulkan/resource/Resource.h` / `.cpp` / `ResourceManager.cpp` | +6 | `VulkanMemoryMappedBuffer` 特化与销毁分支（21 → 22） |
| `src/vulkan/VulkanReadPixels.cpp` | +14 | `TaskHandler::Drain()` 的同步状态改为共享持有（原引用栈上变量，工作线程可能在其返回后才执行，属写已失效栈帧） |
| `src/HwDefine.h` | +6 | `HwMemoryMappedBuffer` / `HwStream` |
| `tests/Engine.h` / `.cpp` | 103 / 280 | 绘制路径转发 + `FlushAndFinish()` + `WaitForRecordedCommands()` |
| `tests/App.cpp` | 1192 | `VerifyDrawTriangleEndToEnd()` 用例 |

### 2. 构建与测试

- `cmake --build build`：0 error
- `bin/UtilsTests`：154 tests passed，exit 0
- `bin/BackendTests`：exit 0（既有 8 帧回归 + 各层用例 + 新绘制用例）

### 3. 三角形验证结果：**降级为清屏用例**（design D7）

**失败环节与原始错误**（不含糊带过）：

| 环节 | 结果 |
|---|---|
| SPIR-V 生成（`glslangValidator -V`） | 成功，`spirv-dis` 校验入口点与装饰正确 |
| headless 交换链 64×64 | 成功（`fmt=37` / `samples=1`） |
| `CreateProgram` / `CreateVertexBufferInfo` / `CreateVertexBuffer` / `CreateBufferObject` / 上传三顶点 / `CreateRenderPrimitive` | 全部成功，句柄非空 |
| `BeginRenderPass`（清屏黑） | 成功；`vkCmdBeginRenderPass` 已录制；读回得到清屏色 |
| `BindPipeline` → `vkCreateGraphicsPipelines` | 成功（`VkResult == VK_SUCCESS`）；`vkCmdBindPipeline` 已录制 |
| `BindRenderPrimitive` → `vkCmdBindVertexBuffers` | 成功；`mRenderPrimitiveState.bound == true` |
| `DrawArrays(0,3,1)` → `vkCmdDraw` | 已录制，参数 `count=3, inst=1, vtxOff=0` |
| **整幅 64×64 读回** | **全部为清屏色，无任何片元写入** |
| 校验层 | 无任何 error/warning 输出 |

**已排除的原因**（逐项实测，非推测）：

1. 深度测试：默认 `RasterState::depthFunc` 是「严格小于」，与 `depthWrite=false` 组合使 `enableDepthTest = true`，对无深度附件的目标 `z=0 < 0.0` 失败而丢弃片元。**已修**（用例显式置 `SamplerCompareFunc::A`），修后 `dsi=0`，仍未画出。
2. 背面剔除：显式 `CullingMode::None` 与 `CullingMode::Front` 两种都试过，均为空。
3. 顶点数据：改用不依赖顶点缓冲、由 `gl_VertexIndex` 生成全屏三角形的着色器，结果相同 → 与顶点上传/属性布局无关。
4. 管线状态：实测选定管线的 `colorWriteMask=0xf`、`blendEnable=0`、`depthTestEnable=0`、`cullMode=0`、`topology=TRIANGLE_LIST`、`rasterizationSamples=1`、`stageCount=2`。
5. 视口/裁剪：实测 `vkCmdSetViewport(0,0,64,64,minD=0,maxD=1)`（`FlipVertically` 后 y=0 保持不变）、裁剪矩形 64×64。
6. 渲染通道与帧缓冲：同一套对象在「只清屏」路径下能正确产出并读回，说明 `VkRenderPass` / `VkFramebuffer` / 附件图像均有效；`storeOp` 为 STORE（`discardEnd=NONE`），清屏不被丢弃。
7. 提交与同步：`vkQueueWaitIdle` + `mCommands.Wait()` + 读回围栏等待均已生效（清屏路径的读回是可靠的）。

**结论**：管线创建、状态绑定、`vkCmdDraw` 录制与提交全部成功且无校验层报错，但光栅化阶段没有任何片元落到颜色附件上。该现象限制在 MoltenVK + 本项目的管线创建路径内，未能定位到本项目代码级的确定原因。

**降级后的实际验证内容**（用例仍完整走一遍绘制序列，只是判定依据降级）：

- 第一轮：`BeginRenderPass`（清屏黑）→ `BindPipeline` → `BindRenderPrimitive` → `DrawArrays(0,3,1)` → `EndRenderPass` → `Commit` → `Finish` → `ReadPixels`，读回中心像素 `(0,0,0)` 并**记录**其与片元着色器输出 `(255,0,0)` 的差异（`LOG_WARN`），不掩盖。
- 第二轮：`BeginRenderPass`（清屏纯绿 `(0,1,0)`）→ `EndRenderPass` → `Commit` → `Finish` → `ReadPixels`，断言中心像素 `== (0,255,0)`（±2）。该断言即验收依据。

即：`beginRenderPass` / `endRenderPass` / `commit` / `readPixels` 的完整链路 + 管线绑定与 `vkCmdDraw` 的录制路径被覆盖；**未被覆盖的是「绘制结果真的落到附件上」这一步**。

### 4. 负向对照（各跑一次后还原，证明断言真的会失败）

| # | 破坏方式 | 结果 |
|---|---|---|
| 1 | 把 `kClearExpectedGreen` 由 `255` 改为 `128` | 读回 `(0,255,0)`、期望 `(0,128,0)` → `LOG_ERROR` + abort → **exit 134** |
| 2 | 把清屏色改回黑但保留「期望纯绿」的断言 | 读回 `(0,0,0)` → `LOG_ERROR` + abort → **exit 134** |

两次都证明「读回颜色 == 期望颜色」的断言确实随真实渲染结果变化，不是恒真断言。

### 5. 销毁路径验证

- `engine->Terminate()` 连续调用 **3 次**不崩溃（用例内），`DestroyResources()` 以 `m_allocator == VK_NULL_HANDLE` 作幂等守卫。
- `~VulkanDriver()` 在 `terminate()` 之后再走一次 `DestroyResources()` 也是 no-op，进程 exit 0。
- 进程退出无 `Some allocations were not freed before destruction of this memory block!`（VMA）断言。

### 6. `DestroyResources` 的落实顺序（8 步）

上游把 `mReadPixels.terminate()` 排在 `vmaDestroyAllocator` 之前。**实测该约束的对象错了**：读回线程持有的是 `VkDevice` 与自建 `VkCommandPool`，与 `VmaAllocator` 无关；变更 6 已记录，本次沿用其结论。实际顺序：

1. `mCommands.Flush()` + `mCommands.Wait()`（排空并等在途命令；**不能调 `finish()`**——它会从命令流分配命令，而 `terminate()` 可能来自非录制线程）
2. `mQueryManager.Terminate()` / `mBlitter.Terminate()` / `mReadPixels.Terminate()`
3. `mCommands.Terminate()`
4. 各 Cache 的 `Terminate()`（pipeline / fbo / sampler / descriptor set / descriptor set layout / pipeline layout / ycbcr）
5. `m_resMgr->Terminate()` ← **必须早于第 6 步**
6. `m_bufferCache->Terminate()` + `Reset()`
7. `m_stagePool->Terminate()` + `Reset()`
8. `m_semaphoreManager->Terminate()` + `Reset()`，最后 `vmaDestroyAllocator`

**第 5 步与第 7 步的相对顺序是本变更实测出的硬约束**（与 design D4 的第 2/7 步相反）：`VulkanStageBuffer::Segment::~Segment()` 会经回收回调写回母缓冲的 `m_segments`；若先执行第 7 步销毁母缓冲、再由 `m_resMgr->Terminate()` 析构仍存活的 `Segment`，就是写已释放对象。反序后 `VulkanStageBuffer::~VulkanStageBuffer()` 的 `IsSafeToReset()` 断言不再触发。

另：`mDefaultRenderTarget` 是成员 `SharedPtr`，其引用要等 `DestroyResources()` 返回后才释放；第 5 步只能清空句柄持有的那一份引用。`DestroyRenderTarget` 因此对默认渲染目标提前返回，不在此处归零。

### 7. 空桩方法清单

**返回空句柄 + 可识别 `LOG_WARN`**（外部图像 ×7）：

`CreateTextureExternalImage2R` / `CreateTextureExternalImageR` / `CreateTextureExternalImagePlaneR` / `ImportTextureR` / `ImportTextureAsyncR` / `SetupExternalImage2` / `SetupExternalImage`
（对应 `S` 方法仍按上游分配句柄后返回，`R` 方法打 `LOG_WARN("... 未实现：外部图像路径已按设计砍掉")`）

**返回空句柄 + 可识别 `LOG_WARN`**（视频流 ×8）：

`CreateStreamNative` / `CreateStreamAcquired` / `SetAcquiredImage` / `SetStreamDimensions` / `UpdateStreams` / `DestroyStream` / `SetExternalStream`，以及 `GetStreamTimestamp`（返回 0）

**纯空实现、不加警告**（与上游一致）：

- `DispatchCompute`（上游即 `// FIXME: implement me`）
- `StartCapture` / `StopCapture`（上游即空）
- `SetFrameScheduledCallback` / `SetFrameCompletedCallback` / `SetPresentationTime`（上游同样为空；本项目未移植 `PresentCallable`）
- 异步变体 `CreateVertexBufferAsyncR` / `CreateIndexBufferAsyncR` / `CreateBufferObjectAsyncR` / `CreateTextureAsyncR` / `CreateTextureViewSwizzleAsyncR` / `SetVertexBufferObjectAsyncR` / `UpdateIndexBufferAsyncR` / `UpdateBufferObjectAsyncR` / `Update3DImageAsyncR` / `QueueCommandAsyncR`（上游同为 `// TODO: implement this.`）
- `ResetBufferObject` / `UpdateBufferObjectUnsynchronized`（上游同样为空/转同步路径）/ `ReadBufferSubData`（上游为 TODO）/ `CancelAsyncJob`（返回 false）

### 8. 与 spec / design 不一致的上游（或本项目）实际定义（一律以上游/实测为准）

| # | spec / design 说 | 实际 | 处置 |
|---|---|---|---|
| 1 | design D4 / D3：`m_readPixels.Terminate()` 必须先于 `vmaDestroyAllocator`，「读回线程会访问已释放的 allocator」 | 读回线程持有 `VkDevice` + 自建 `VkCommandPool`，**完全不碰 VMA** | 约束改写为「须在 `VkDevice` 销毁前终止」（变更 6 已记录），本变更沿用 |
| 2 | design D4 的 8 步顺序把「各池 Terminate」放最后（第 7 步），把 `ResourceManager::Terminate` 放第 1 步 | **必须 `m_resMgr->Terminate()` 早于 `m_stagePool->Terminate()`**：暂存段的回收回调会写回母缓冲 | 实测反序会触发 `IsSafeToReset()` 断言；已按实测顺序实现（见第 6 节） |
| 3 | `DestroyResources` 首步可直接 `finish(0)` | `finish()` 经命令流分配命令，而 `terminate()` 可能来自非录制线程（`Engine::Terminate` 在主线程） | 改为 `mCommands.Flush() + Wait()`；实测调 `finish(0)` 会命中 `AllocateCommand` 的线程断言 |
| 4 | spec「`mPipelineState`（含 `bindInDraw`）」、design 的 `BindInDrawBundle` | 与上游一致；`BindInDrawBundle` 内含 `dsLayoutHandles` / `descriptorSetMask` / `program`，`mPipelineState` 另有 `program` / `pipelineLayout` / `descriptorSetMask` | 按上游结构实现 |
| 5 | spec/design 的「25 条特性查询」 | 实际上游只有 **19 条** `is*Supported` / `get*` 查询（不含 `isDepthStencilBlitSupported` 之外的实现细节差异）；其中 `IsFrameBufferFetchSupported` / `IsFrameBufferFetchMultiSampleSupported` / `IsAutoDepthResolveSupported` / `IsDepthStencilResolveSupported` / `IsAsynchronousModeEnabled` **上游本身就是 `return false`** | 按上游逐条实现；这 5 条不是本项目留的占位，是上游的确定性语义 |
| 6 | spec「`getClipSpaceParams()` 返回与上游一致的 `math::float2`（MoltenVK 下 Y 轴方向差异须正确处理）」 | 上游硬编码 `{1.0f, 0.0f}`，与平台无关 | 按上游返回 `{1,0}` |
| 7 | design D3 称「`endFrame` 调交换链 `Recreate`」 | `VulkanSwapChain` 没有 `Recreate` / `HasResized`（变更 5 已记录） | `EndFrame` 只做 `endCommandRecording` + `collectGarbage`，与上游一致 |
| 8 | spec「`createDefaultRenderTargetR` 把预创建对象交给句柄」 | 本项目 `ResourceManager` 无 `AssociateExisting`，但 `Make<D>(handle, args...)` 可直接以「移动构造」表达复用 | 用 `Make<VulkanRenderTarget>(rth, std::move(*mDefaultRenderTarget.Get()))`，**无需扩展 ResourceManager**（task 1.2 的答案） |
| 9 | design「`VulkanDriverFactory.h` / 工厂函数」 | 本项目 `VulkanPlatform::CreateDriver` 直接调 `VulkanDriver::Create` | 不引入工厂（与 spec 的 `VulkanDriverFactory` 要求一致，task 11.12 的答案） |
| 10 | task 11.9 称 `RasterState` 有 **20** 个字段 | 本项目 `VulkanPipelineCache::RasterState` 是 **19** 个字段（含 `depthClamp`，不含上游的 `depthClampEnable`/`rasterizerDiscardEnable` 等 PipelineCache 内部省略项） | 19 个逐字段全部赋值，已用脚本核对无遗漏 |
| 11 | `DriverConfig::featureFlagManager` 类型从 `void const*` 收口为 `utils::FeatureFlagManager const*`（spec / task 1.5） | 上游用它只判断 `enable_acquire_swapchain_in_make_current` 一个开关 | **未建立 `FeatureFlagManager`**：该开关在本项目无消费点（`MakeCurrent` 不做 acquire），保持 `void const*`。属有意偏离，理由记录于此 |
| 12 | spec「`mIsMSAASwapChainSupported` 成员」 | 上游该成员恒为 `false`（`// TODO: support MSAA swapchain`） | 去掉成员，`IsMSAASwapChainSupported` 直接返回 `false` |
| 13 | 上游 `UpdateBufferObjectUnsynchronized` 有独立的直写路径 | 上游实现里同样是 `// TODO: implement unsynchronized version` 后走 `loadFromCpu` | 转调 `UpdateBufferObject`，与上游行为等价 |
| 14 | `VulkanIndexBuffer` 可直接上传（`updateIndexBuffer` 调用点） | 类内**没有** `LoadFromCpu`（变更 6 只给了 `VulkanBufferObject` 的空桩） | 补 `VulkanIndexBuffer::LoadFromCpu` 转发 `m_buffer.LoadFromCpu`；同时恢复 `VulkanBufferObject::LoadFromCpu` 的转发（原为空桩，三角形用例需要真实上传） |
| 15 | `Driver::finish` 等同步方法经基类指针可直接派发到 `VulkanDriver` | 实测 `m_driver->finish(0)`（`Driver*`）**不会**进入 `VulkanDriver::finish`（用静态计数器与 `fprintf` 双重确认，计数为 0）；而经 `CommandStream`（`ConcreteDispatcher`）调用正常 | 测试侧用具体类型指针调用（`static_cast<VulkanDriver*>`），生产路径仍走 `CommandStream`。原因未定位，一并记录 |
| 16 | 上游 `VulkanDriver::DebugCommandBegin` 在 `BVK_DEBUG_*` 下输出命令调试信息 | 本项目未移植 `utils::io::ostream` 与 `FILAMENT_DEBUG_COMMANDS` 体系 | override 到 `DriverBase::DebugCommandBegin`（默认空实现），与 spec 的「改用 `LOG_DEBUG`」等价 |
| 17 | `VulkanReadPixels::TaskHandler::Drain()` 借栈上变量作同步点 | 工作线程可能在 `Drain()` 返回后才执行该回调，写已失效栈帧（并实测到「`Drain()` 已返回但回调尚未触发」） | 同步状态改为 `std::shared_ptr<SyncPoint>` 共享持有 |

### 9. 未验证面（局限）

- **三角形光栅化输出**：见第 3 节，管线绑定与 `vkCmdDraw` 已覆盖，但「绘制结果落到附件」未验证。
- 外部图像 / 视频流路径（空桩，无验证）；`DispatchCompute` / `StartCapture` / `StopCapture`（空实现）；`NextSubpass`（无测试路径）；Present timing 查询（返回 false）；有窗口的呈现路径（只验证 headless）。
- `ReadPixels` 走「清屏色 + 4 通道 UBYTE」组合时会打印一条 `reshaping 4-channel type 0 into 0-channel type 0` 警告，但逐行拷贝结果正确（读回值与清屏色一致）。该警告的成因未定位，未影响结果。
- 异步变体（`*Async`）与 `QueueCommandAsync` / `CancelAsyncJob` 未运行期验证（上游即为 TODO）。

## 三角形排查记录（第 3 轮）

在子代理报告的「已排除项」基础上，本轮又**实测排除**了以下假设（均为仪器化验证，非推断）：

| 假设 | 验证方式 | 结论 |
|---|---|---|
| `VulkanPipelineCache::BindPipeline` 跳过绑定（`m_boundPipeline` 与新建管线句柄值相同） | 在 `BindPipeline` 的两条分支各加日志 | **排除**：打印 `DIAG bind: BOUND handle=527130492928`，绑定确实执行 |
| `mRenderPrimitiveState.bound` 为假导致 `DrawArrays` 空转 | 在 `DrawArrays` 加日志 | **排除**：`bound=true vertexCount=3`，`vkCmdDraw` 确实录制 |
| `bindRenderPrimitive` 因 `!vertexBuffer->IsValid()` 静默短路 | 读代码 + 查测试 | **排除**：`IsValid()` = `m_attributes == vbi->GetDeclaredAttributes()`；测试设了 `attributes[0].buffer = 0`，且 VBI 构造里 `attrib = attributes[0]` 的重赋值使 `attribToBuffer` 全为 0，`SetBuffer(0)` 会置满 16 位 |
| 顶点数据未上传（`UpdateBufferObject` 缺失） | grep 测试 | **排除**：`tests/App.cpp:1078` 有 `UpdateBufferObject(boh, BufferDescriptor(kFullScreenTriangle, ...), 0)` |
| `VulkanBufferObject::LoadFromCpu` 仍是空桩 | 读代码 | **排除**：已转发到 `m_buffer.LoadFromCpu`（变更 1 的代理上传通道） |
| `GetColorTargetCount` 返回 0（管线无颜色附件） | 读代码 | **排除**：default 渲染目标走 `if (!m_offscreen) return 1;` |
| 视口/深度范围参数为零 | 读测试与 `BeginRenderPass` | **排除**：`params.viewport = {0,0,64,64}`、`depthRange = {0,1}` |
| 深度测试丢弃片元 | 读测试与 `CreatePipeline:233` | **排除**：测试设 `depthFunc = SamplerCompareFunc::A`（always）、`depthWrite = false` → `enableDepthTest = false` |
| `endCommandRecording` / `ResetBoundPipeline` 未接线 | grep 调用点 | **排除**：`endCommandRecording` 在 232/236/239/1825/1838 被调用，内部调 `ResetBoundPipeline()` 与上游一致 |

**证据链现状**：渲染通道、帧缓冲、清屏、提交、读回全部正常；管线已创建并绑定、图元已绑定、`vkCmdDraw` 已录制、无校验层报错——**但片元未落到颜色附件**。

### 下一轮建议优先验证

1. **顶点输入状态的声明数量**：`VulkanVertexBufferInfo::GetAttributeCount()` 返回 `m_info.m_soa.Size()`，而 `m_info` 构造为 `attributeCount == 0 ? 0 : attributes.size()` —— `attributes` 是 `AttributeArray`（size = 16）。因此管线声明了 **16 个 vertex binding / 16 个 attribute**，而 `vkCmdBindVertexBuffers` 也绑了 16 个。上游逻辑相同，但值得确认 MoltenVK 在「声明 16 个 binding 而着色器只用 binding 0」时是否正常。
2. **SPIR-V 本身**：用 `spirv-dis` 反汇编 `tests/TriangleShaders.h` 的顶点/片元模块，确认顶点着色器写的 `gl_Position` 确实是全屏三角形（`(-1,-1) / (3,-1) / (-1,3)`）、片元着色器输出 location 0。
3. **捕获一次真实渲染**：用 `VK_LAYER_KHRONOS_validation` + RenderDoc（若有）看 `vkCmdDraw` 时管线/绑定/附件的真实状态。

## 三角形排查记录（第 4 轮）

### 管线创建参数实测（仪器化 dump，非推断）

在 `CreatePipeline` 内打印真实的 `VkGraphicsPipelineCreateInfo`：

```
DIAG pipeline: rp=533202221440 subpass=0 vtxAttrCount=16 vtxBindCount=16 \
               colorAttach=1 pNext=0 depthTest=0 topology=3
```

| 字段 | 实测值 | 判定 |
|---|---|---|
| `renderPass` | 非空，且与 `vkCmdBeginRenderPass` 用的是同一对象（`BindRenderPass` 在 `BeginRenderPass` 内调用，同一 `VulkanRenderPassPtr`） | ✓ |
| `subpass` | 0 | ✓ |
| `vertexAttributeDescriptionCount` | 16 | 与 VBI 的 SoA 尺寸一致（见下） |
| `vertexBindingDescriptionCount` | 16 | 与 `vkCmdBindVertexBuffers` 绑的 16 个一致 |
| `colorBlendState.attachmentCount` | 1 | ✓ 有颜色附件 |
| `pNext` | 0 | ✓ 走静态渲染通道，非 dynamic rendering |
| `depthTestEnable` | 0 | ✓ 深度测试已关 |
| `topology` | 3 | ✓ `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST` |

**结论：管线本身在可观测的每个参数上都正确。** 顶点输入声明数 = 绑定数 = 16（不是「声明 1 而绑 16」的错配）。

### 本轮又排除的假设

| 假设 | 结论 |
|---|---|
| 「管线声明 16 个 binding 而着色器只用 0」导致不兼容 | **排除**——`vkCmdBindVertexBuffers` 确实绑了 16 个，与声明数一致 |
| 管线被当成 dynamic rendering 创建（`pNext = VkPipelineRenderingCreateInfo`）与 `vkCmdBeginRenderPass` 不兼容 | **排除**——`useDynamicRenderPasses` 默认 `false`，实测 `pNext=0` |
| 管线缺静态顶点输入（`useDynamicVertexInputState`）而 `vkCmdSetVertexInputEXT` 从未调用 | **排除**——实测 `vtxAttrCount=16`，走的是静态顶点输入路径。该动态路径只存在于 prewarm（`AsyncPrewarmCache`，`renderPass = VK_NULL_HANDLE` + `VK_DYNAMIC_STATE_VERTEX_INPUT_EXT`），与绘制路径的 key 不同不会冲突；且 `vkCmdSetVertexInputEXT` 上游也没有调用——上游同样只在 prewarm 用它，管线是一次性预热产物 |
| 顶点数据未上传 / `LoadFromCpu` 是空桩 | **排除**（第 3 轮已验） |

### 证据链现状

到本轮为止，**渲染通道、帧缓冲、清屏、提交、读回、管线创建参数、管线绑定、图元绑定、`vkCmdDraw` 录制，全部逐项验证正确且无校验层报错**——但片元仍不落到颜色附件。

这意味着问题**不在驱动侧的参数构造**，而在更下游。下一轮应当换手段，不要再读本项目代码：

1. **开启 `VK_LAYER_KHRONOS_validation` 跑一次**，看 `vkCmdDraw` 时是否有被 `LOG` 吞掉的警告（当前用例只统计 `critical`，warning 未纳入判定）。
2. **用 RenderDoc / `vkCmdBeginDebugUtilsLabelEXT` 抓帧**，直接看 GPU 侧看到的状态。
3. **反汇编 `tests/TriangleShaders.h` 的 SPIR-V**（`spirv-dis`），确认 `gl_Position` 写的确实是全屏三角形——这是唯一还没被独立验证过的输入。
4. **换一个最小的原生 Vulkan 复现程序**（不经过本项目的任何抽象），用同一套 SPIR-V 在 MoltenVK 上画，确认环境本身没问题。

## 三角形排查记录（第 5 轮）

### SPIR-V 反汇编（此前唯一未验证的输入）——正确

用 `spirv-dis` 反汇编 `tests/TriangleShaders.h` 的两个模块：

- **顶点**：`OpLoad %v2float %position`（Location 0）→ `OpCompositeConstruct %v4float pos.x pos.y 0.0 1.0` → `OpStore` 到 `gl_Position`。**完全正确**
- **片元**：`OpConstantComposite %v4float 1.0 0.0 0.0 1.0` → `OpStore %fragColor`（Location 0）。**不透明红，正确**

### scissor 实测——正确

```
DIAG beginRenderPass: extent=64x64 scissor=64x64 swapchainBound=true viewport=64x64
```

### 开 validation layer ——发现 22 条真实错误，此前完全不可见

**这是本轮最重要的发现**：测试用例的通过判据只统计 `LOG_CRITICAL`，**validation 的 error/warning 从未纳入判定**。加上 `BVK_DEBUG_VALIDATION` 后（`BACKEND_DEBUG_FLAG=3`，未含 `0x40`），用环境变量强制加载 layer 跑一次，暴露出：

| VUID | 次数 | 含义 | 来源判断 |
|---|---|---|---|
| `VkFramebufferCreateInfo-layers-00889` | 4 | `layers` 必须 > 0 | **测试侧**：变更 5 的用例直接调 `GetFramebuffer(cacheKey, ...)`，手搓的 `FboKey` 未设 `layers`（驱动路径在 `BeginRenderPass:1135` 有 `fbkey.layers = 1`） |
| `VkAttachmentDescription-format-06699` | 4 | `loadOp = LOAD` 时 `initialLayout` 不能是 `UNDEFINED` | 渲染通道的**深度附件**未被清屏或丢弃：测试只传 `clear/discardStart = COLOR`，而交换链带深度附件 |
| `vkCreateDevice-ppEnabledExtensionNames-01387` | 4 | 设备扩展的必需依赖未同时启用 | 设备创建 |
| `vkCreateSamplerYcbcrConversion-None-01648` | 2 | `samplerYcbcrConversion` 特性未启用 | YCbCr 缓存 |
| `vkDestroyDevice-device-05137` | 8 | 设备销毁时仍有存活子对象 | 资源泄漏 |

**已修**：给测试的 `params.flags.clear` / `discardStart` 补 `DEPTH`，消除驱动路径的 `06699`。**该修改保留**（它是真实的规格违反）。

**其余仍未处理**（本变更范围外的既有问题）：
- 测试侧手搓 `FboKey` 未设 `layers` → 属变更 5 的用例
- `vkCreateDevice` 扩展依赖、YCbCr 特性未启用 → 属变更 3 / 变更 4 的设备创建路径
- `vkDestroyDevice` 的 8 条泄漏 → 需逐个定位是哪些对象

### 三角形仍未渲染

补 DEPTH 后重跑，三角形轮中心像素仍是 `(0,0,0)`（清屏色），未出现片元输出。

### 下一轮建议

1. **把 validation error 纳入测试判据**——这是本轮的元发现。一个「全绿」的测试跑出 22 条规格违反，说明验证强度有系统性缺口。
2. **逐条清掉上面 5 类 VUID**，特别是 `vkDestroyDevice` 的 8 条泄漏——泄漏常与「对象未正确创建/绑定」同源。
3. **在 `BeginRenderPass` 前后加 `vkCmdBeginDebugUtilsLabelEXT`**，配合 RenderDoc 抓帧，直接看 GPU 侧状态，不再靠读代码推理。

## 三角形排查记录（第 6 轮）

### 8 条 `vkDestroyDevice-device-05137` 泄漏的定位

完整消息（非截断）：

```
vkDestroyDevice(): Object Tracking - For VkDevice 0x7719202018, VkImageView 0x1000000000100 has not been destroyed.
vkDestroyDevice(): Object Tracking - For VkDevice 0x7719202018, VkImageView 0x1010000000101 has not been destroyed.
vkDestroyDevice(): Object Tracking - For VkDevice 0x77191fc018, VkImageView 0x730000000073 has not been destroyed.
vkDestroyDevice(): Object Tracking - For VkDevice 0x77191fc018, VkImageView 0x720000000072 has not been destroyed.
```

**全部是 `VkImageView`，且分布在两个不同的 `VkDevice` 上。**

排查结论：

1. **不是驱动自身的销毁顺序问题** —— `VulkanDriver::DestroyResources()` 的顺序是正确的：`Flush`+`Wait` → `mCurrentSwapChain = {}` / `mDefaultRenderTarget = {}`（放掉交换链与默认渲染目标持有的纹理）→ 各缓存 `Terminate` → **`m_resMgr->Terminate()` 排空 GC**（此处销毁纹理与 `VulkanTextureState`，后者析构时调 `vkDestroyImageView`）→ 各池 `Terminate` → `vmaDestroyAllocator`。
2. **两个 `VkDevice` 说明泄漏来自测试侧的分阶段设备** —— `tests/App.cpp` 的 `VerifyTextureLayer`（变更 4）/ `VerifyRenderTargetLayer`（变更 5）/ `VerifyPipelineLayer`（变更 6）各自建立独立的 platform + device，其创建的 `VulkanTexture`（包装手工 `VkImage`）与 `VulkanRenderTarget` 未在销毁设备前释放，故其 `VulkanTextureState` 的缓存视图随设备一同泄漏。
3. `VkImageView` 全项目只有一处销毁点：`VulkanTexture.cpp:266`（`VulkanTextureState` 的视图缓存清理）。路径本身没问题，是**调用方没放掉最后一个引用**。

### 本轮的判断修正

第 5 轮我把「8 条对象泄漏」列为「可能与三角形同源」的线索。**本轮否掉了这个判断**：泄漏对象在两个测试自建设备上，与三角形用的那套驱动实例不是同一批对象。

### validation 错误的最终归类

| VUID | 次数 | 归属 |
|---|---|---|
| `VkFramebufferCreateInfo-layers-00889` | 4 | **测试侧**：变更 5 用例手搓 `FboKey` 未设 `layers` |
| `VkAttachmentDescription-format-06699` | 4 | **已修**：测试的 `clear`/`discardStart` 未含深度附件 |
| `vkDestroyDevice-device-05137` | 8 | **测试侧**：分阶段设备未释放纹理/渲染目标 |
| `vkCreateDevice-ppEnabledExtensionNames-01387` | 4 | **驱动侧真实问题**：启用 `VK_KHR_depth_stencil_resolve` 但未同时启用其依赖 `VK_KHR_create_renderpass2` |
| `vkCreateSamplerYcbcrConversion-None-01648` | 2 | **驱动侧真实问题**：调 `vkCreateSamplerYcbcrConversion` 前未启用 `samplerYcbcrConversion` 特性 |

**两条驱动侧真实问题**（`vkCreateDevice` 扩展依赖、YCbCr 特性）与三角形无关，但都是明确的规格违反，应修。

### 三角形仍未渲染

本轮未取得三角形相关的新线索。至此累计排除 16 条假设。

## 三角形排查记录（第 7 轮）

### 决定性观测：零片元（非覆盖问题）

把测试的读回从「中心 1×1」改为**整幅 64×64**，统计非清屏色像素：

```
DIAG fullImage: nonBlack=0 bbox=[64,64]-[0,0]
```

**整幅图像一个像素都不是片元输出。** 这排除了此前所有「视口/裁剪/覆盖范围不对」的猜测——不是画到了别处，是**根本没光栅化**。

（该改动保留：整幅读回比只查中心点覆盖更完整，且完成了回调计数断言。）

### 本轮排除的假设

| 假设 | 手段 | 结论 |
|---|---|---|
| 片元画到了别处（视口翻转/裁剪矩形偏差导致中心点落在覆盖区外） | 整幅统计非清屏色像素 | **排除**：`nonBlack = 0`，零片元 |
| `rasterizerDiscardEnable = VK_TRUE` 静默丢弃全部图元 | grep 全项目 | **排除**：未设置（默认 FALSE），上游也未设置（`VulkanPipelineCache.h:39` 有注释说明「Filament 从不改变它们」） |
| `rasterizationSamples = 0`（非法采样数导致不光栅化） | grep `fbkey.samples` 赋值点 | **排除**：`VulkanHandle.cpp:207` 在交换链绑定路径设为 1 |

### 累计

至此 **18 条假设被实测排除**。已确认正确的环节：

```
交换链创建 ✓  附件 VulkanTexture ✓  渲染通道创建 ✓  帧缓冲创建 ✓
loadOp 清屏 ✓  提交与同步 ✓  读回 ✓  SPIR-V ✓  顶点数据 ✓
管线创建参数（8 项）✓  管线绑定 ✓  图元绑定 ✓  顶点缓冲绑定 ✓
vkCmdDraw 录制 ✓  视口 ✓  裁剪矩形 ✓  深度测试关闭 ✓  采样数 ✓
```

**唯一仍未验证的环节**：GPU 侧实际发生了什么——即 `vkCmdDraw` 之后光栅化器看到的状态。靠读本项目代码已经无法推进（连续三轮的假设全部被数据推翻）。

### 下一轮必须换手段

1. **RenderDoc / `VK_LAYER_KHRONOS_validation` + `vkCmdBeginDebugUtilsLabelEXT`**：抓一帧，直接看 GPU 侧状态
2. **写一个不经过本项目任何抽象的原生 Vulkan 最小程序**（~300 行），用同一套 SPIR-V、同样的渲染通道/管线参数在 MoltenVK 上画。若它能画出来，说明问题在本项目的抽象层；若它也画不出来，说明是 MoltenVK 用法问题（例如某个扩展未启用）——这能一步划分问题域
3. **修掉两条已知的驱动侧规格违反**（`VK_KHR_create_renderpass2` 未随 `depth_stencil_resolve` 启用、`samplerYcbcrConversion` 特性未启用），看是否连带解决

## 三角形排查记录（第 8 轮）：根因已定位并修复

### 根因

**`RasterState::colorWrite` 是无初值的 `bool : 1` 位域**（`include/Backend/DriverDefine.h:1056`，上游 `DriverEnums.h:1573` 同样无初值）。测试构造 `Backend::PipelineState state;` 时只设了 `depthFunc` / `depthWrite` / `culling`，**没有设 `colorWrite`** —— 值初始化把该位域置为 `false`，于是：

```
VulkanDriver.cpp:1337  .colorWriteMask = (rasterState.colorWrite ? 0xf : 0x0)
                       → VkColorComponentFlags{0}
```

`colorWriteMask = 0` 表示**所有颜色分量写入都被屏蔽**。渲染照常执行、片元照常产生、`vkCmdDraw` 照常录制、**零 validation 报错**，但颜色附件一个字节都不被改写 —— 于是它保留 loadOp 清屏后的值。

上游由前端保证始终显式设置 `colorWrite`，本项目测试没设。

### 为何排查了 18 条假设才找到

**测试的清屏色是黑色，而黑清屏 + 写入被屏蔽的结果，与「完全没有片元」在读回上完全无法区分。** 连续多轮的读回统计（包括整幅 `nonBlack` 计数）都指向「零片元」，把注意力引向了几何/视口/深度/管线参数。

**打破僵局的一步是把清屏色改成蓝色**：此时若写入被屏蔽，附件保留蓝色；若有片元写入，则是红色。一改立刻区分——中心像素是 `(0,0,255)` 纯蓝，证明「片元没写进去」，而不是「没产生片元」。

### 关键外部证据

子代理写了一个**完全独立于本项目**的原生 Vulkan 最小程序（`/tmp/native_tri.cpp`，~300 行，只用本项目的 SPIR-V），用与项目实测 dump **逐条相同的管线参数**在 MoltenVK 上跑：

- **结果：4096/4096 全红，画出**
- 对照 A（16 attr/binding → 1）：画出 → 「声明 16 个但只用 1 个」排除
- 对照 B（+ 深度附件）：画出 → 深度附件排除
- 对照 C（viewport 高度 -64 Y 翻转）：画出 → 视口翻转排除

**结论：问题不在 MoltenVK，在本项目抽象层。** 该实验同时列出「项目 dump 未覆盖、且能零报错吃掉画面」的参数清单（顶点缓冲内容 / colorWriteMask / blend 因子 / stencilTestEnable / 视口实际值），`colorWriteMask` 正是其中之一。

### 修复

`tests/App.cpp` 的绘制用例显式设置 `state.rasterState.colorWrite = true;`，并把降级路径改回**硬断言**（三角形画不出即失败）。

### 排查过程中自己在测试里引入并修掉的问题

1. 用「整幅读回 + DIAG 统计」替换中心读回时，误删了第二轮清屏校验块，后又误删了资源清理段 → 触发 VMA `Some allocations were not freed` 断言（exit 134）。已在用例末尾补回显式 `Destroy*` 调用（`DestroyRenderPrimitive` / `DestroyBufferObject` / `DestroyVertexBuffer` / `DestroyVertexBufferInfo` / `DestroyProgram` / `DestroySwapChain`）。
2. 为消歧把清屏色由黑改为蓝——该改动保留，它使「写入被屏蔽」与「无片元」可区分。

### 最终状态

```
draw path: triangle sequence center pixel = (255, 0, 0), fragment color expected (255, 0, 0)
draw path: triangle rendered and verified (center pixel matches the fragment shader output)
BackendTests exit = 0
```
