# Capability: vulkan-driver

## Purpose

Vulkan 驱动的收口层：把前 6 个变更交付的 17 个组件接进 `VulkanDriver`，实现 143 条驱动方法、完整帧循环、绘制路径与资源生命周期。上游对应物为 `backend/src/vulkan/VulkanDriver.{h,cpp}`（236 + 2956 行）。

这是唯一产出可观察结果的层——本能力域完成后，后端可渲染出三角形。

## ADDED Requirements
### Requirement: VulkanDriver 成员与基类

`VulkanDriver` SHALL 继承 `DriverBase`（而非 `Driver`），并持有上游的 17 个成员：

- `VulkanPlatform* mPlatform`
- `ResourceManager mResourceManager`（本项目为 `ResourceManagerPtr`）
- `VulkanSwapChainPtr mCurrentSwapChain`
- `VulkanRenderTargetPtr mDefaultRenderTarget`
- `VulkanRenderPassContext mCurrentRenderPass`
- `VmaAllocator mAllocator`
- `VulkanContextPtr mContext`
- `VulkanSemaphoreManagerPtr mSemaphoreManager`
- `VulkanCommands mCommands`
- `VulkanPipelineLayoutCache mPipelineLayoutCache`
- `VulkanPipelineCache mPipelineCache`
- `VulkanStagePoolPtr mStagePool`
- `VulkanBufferCachePtr mBufferCache`
- `VulkanFboCache mFramebufferCache`
- `VulkanYcbcrConversionCache mYcbcrConversionCache`
- `VulkanSamplerCache mSamplerCache`
- `VulkanBlitter mBlitter` / `VulkanReadPixels mReadPixels`
- `VulkanDescriptorSetLayoutCache mDescriptorSetLayoutCache` / `VulkanDescriptorSetCache mDescriptorSetCache`
- `VulkanQueryManager mQueryManager`

外加状态结构体：`mPipelineState`（含 `bindInDraw`）/ `mAppState` / `mRenderPrimitiveState`。

成员声明顺序 SHALL 与构造初始化列表顺序一致（`-Wreorder` 校验）。

#### Scenario: 成员齐备

- **WHEN** 对照上游 `VulkanDriver.h` 的成员清单
- **THEN** 17 个组件成员全部存在

#### Scenario: 基类正确

- **WHEN** 检查 `VulkanDriver` 的继承列表
- **THEN** 为 `DriverBase`，且 `debugCommandBegin` 被 override

#### Scenario: 初始化顺序

- **WHEN** 以 `-Wreorder` 构建
- **THEN** 无「成员初始化顺序与声明顺序不一致」警告

### Requirement: 构造期预创建默认渲染目标

`VulkanDriver` 构造函数 SHALL 在成员初始化列表中创建 `mDefaultRenderTarget`，**早于** `createDefaultRenderTarget` 被调用。

理由（上游注释）：该顺序解除了「`createDefaultRenderTarget` 必须先于 `makeCurrent`」的约束，使前端可自由排列调用顺序。

`createDefaultRenderTargetR` SHALL 把预创建的对象交给传入的句柄，不重新构造附件。

#### Scenario: 构造后默认渲染目标存在

- **WHEN** `VulkanDriver` 构造完成
- **THEN** `mDefaultRenderTarget` 非空，`IsSwapChain()` 返回 true，附件为空

#### Scenario: createDefaultRenderTarget 不重建

- **WHEN** 调用 `CreateDefaultRenderTargetR(rth)`
- **THEN** 句柄关联到预创建的对象，未发生第二次构造

#### Scenario: 调用顺序无约束

- **WHEN** 先调用 `CreateDefaultRenderTargetR` 再 `MakeCurrent`，或反之
- **THEN** 两种顺序均正确工作

### Requirement: 帧循环

`VulkanDriver` SHALL 实现：

- `tick(int)`：`mResourceManager.Gc()` + `mFramebufferCache.Gc()` + `mPipelineCache.Gc()` + `mQueryManager.Reset()` + `mCommands.Gc()` / `UpdateFences()`
- `beginFrame(monotonic_clock_ns, refreshIntervalNs, frameId)`：`mCommands.UpdateFences()` + 记录帧起始
- `endFrame(frameId)`：交换链 `Recreate`（若 `HasResized`）
- `flush(int)`：`mCommands.Flush()` + `mBufferCache.Gc()` + `mStagePool.Gc()`（按 `FVK_MAX_FRAMES_IN_FLIGHT` 节流）
- `finish(int)`：`mCommands.Wait()`
- `resetState(int)`：重置 `mPipelineState` 与 `mRenderPrimitiveState`

#### Scenario: tick 推进 GC

- **WHEN** 帧循环调用 `Tick()`
- **THEN** 各 Cache 的 `Gc()` 被执行，引用归零的资源被回收

#### Scenario: flush 提交命令

- **WHEN** 调用 `Flush()`
- **THEN** `VulkanCommands::Flush()` 提交已录制的命令缓冲；无命令时不提交

#### Scenario: finish 等待完成

- **WHEN** 调用 `Finish()`
- **THEN** `VulkanCommands::Wait()` 阻塞至全部提交完成

### Requirement: 绘制路径

`VulkanDriver` SHALL 实现：

- `beginRenderPass(Handle<HwRenderTarget>, RenderPassParams const&)`：交换链 acquire（若为默认渲染目标且首次渲染通道）→ 取命令缓冲 → 缓存 `VkRenderPass` / `VkFramebuffer` → 发射屏障 → `vkCmdBeginRenderPass` → `vkCmdSetScissor` / `vkCmdSetViewport` → 设置 `mCurrentRenderPass`
- `endRenderPass(int)`：`vkCmdEndRenderPass` → 发射结束屏障 → 清空 `mCurrentRenderPass`
- `bindPipeline(PipelineState const&)`：收集 descriptor set 布局 → 计算 `descriptorSetMask` → `GetLayout` → `bindPipelineImpl`
- `bindPipelineImpl`：填充 `VulkanPipelineCache::RasterState`（20 字段）→ 绑定状态 → `vkCmdBindPipeline`
- `bindRenderPrimitive(Handle<HwRenderPrimitive>)`：绑定顶点缓冲与索引缓冲
- `bindDescriptorSet(Handle<HwDescriptorSet>, descriptor_set_t, DescriptorSetOffsetArray&&)`
- `prepareDraw()`：处理 `bindInDraw` 延迟布局绑定 + `mDescriptorSetCache.Commit(...)`
- `draw2(indexOffset, indexCount, instanceCount)` → `vkCmdDrawIndexed`
- `drawArrays(vertexOffset, vertexCount, instanceCount)` → `vkCmdDraw`
- `draw(PipelineState, rph, indexOffset, indexCount, instanceCount)`：`bindPipeline` + `bindRenderPrimitive` + `draw2`
- `scissor(Viewport)`
- `makeCurrent(drawSch, readSch)` / `commit(sch)`（present）
- `setPushConstant(stage, index, value)`

空渲染通道下的全部绘制方法 SHALL 提前返回（`skipDueToEmptyRenderPass()`），使未调用 `beginRenderPass` 时的绘制退化为 no-op 而非崩溃。

#### Scenario: 完整绘制序列

- **WHEN** 依次调用 `beginRenderPass` / `bindPipeline` / `bindRenderPrimitive` / `drawArrays` / `endRenderPass`
- **THEN** 命令缓冲中录制出完整的渲染通道，无校验层报错

#### Scenario: 空渲染通道下绘制安全

- **WHEN** 未调用 `beginRenderPass` 直接调用 `bindPipeline` / `bindRenderPrimitive` / `draw2`
- **THEN** 全部提前返回 no-op，不崩溃

#### Scenario: 无描述符集的管线

- **WHEN** `PipelineState::pipelineLayout.setLayout` 全为空时 `bindPipeline`
- **THEN** `descriptorSetMask` 为 `0x1`，`Commit` 因未 stash 而清除该位，不调用 `vkCmdBindDescriptorSets`

#### Scenario: 交换链 acquire 失败

- **WHEN** `AcquireNextSwapchainImage()` 返回 false
- **THEN** `beginRenderPass` 清空 `mCurrentRenderPass` 并返回，后续绘制退化为 no-op

#### Scenario: RasterState 填充完整

- **WHEN** 检查 `bindPipelineImpl` 中对 `VulkanPipelineCache::RasterState` 的 20 个字段赋值
- **THEN** 每个字段均被赋值，与上游逐字段一致

### Requirement: 资源创建与销毁

`VulkanDriver` SHALL 实现全部 `create*` / `destroy*` 方法对，覆盖：顶点缓冲（Info）/ 索引缓冲 / 缓冲对象 / 纹理 / texture view / 程序 / 渲染图元 / 渲染目标 / 交换链 / 围栏 / 同步 / 计时查询 / 描述符集（Layout）/ 内存映射缓冲。

销毁方法 SHALL 全部对空句柄提前返回（`if (!handle) return;`）。

`CreateDefaultRenderTarget` / `CreateRenderTarget` 的 offscreen 路径 SHALL 从 `MRT` / `TargetBufferInfo` 构造附件。

#### Scenario: 空句柄销毁安全

- **WHEN** 对默认构造（空）的句柄调用任意 `Destroy*` 方法
- **THEN** 提前返回，不崩溃

#### Scenario: 创建-销毁成对

- **WHEN** 对每个资源类型执行 `Create*` → `Destroy*`
- **THEN** `Terminate` 后 `ResourceManager` 的泄漏计数为 0

### Requirement: 特性查询

`VulkanDriver` SHALL 实现 25 条 `is*Supported` / `get*` 查询，**逐条对照上游实现，不允许占位返回值**。

返回值错误会导致前端选择不存在的路径。典型：
- `getClipSpaceParams()` 决定 Y 轴方向
- `getMaxDrawBuffers()` 决定 MRT 上限
- `isTextureFormatSupported()` 决定纹理创建可行性
- `getFeatureLevel()` 决定可用的渲染特性

实现来源分四类：查 `VulkanContext` 特性位、查 `VkPhysicalDeviceLimits`、常量返回、查格式表。

#### Scenario: 无占位实现

- **WHEN** 检查 25 条查询的实现体
- **THEN** 每条均有实质逻辑（查特性位 / 查 limits / 常量 / 查表），无「一律返回 false」的占位

#### Scenario: clip space 参数

- **WHEN** 调用 `GetClipSpaceParams()`
- **THEN** 返回与上游一致的 `math::float2`（MoltenVK 下 Y 轴方向与桌面 Vulkan 的差异须正确处理）

#### Scenario: 格式支持查询

- **WHEN** 对 `TextureFormat::R8G8B8A8_UNORM` 调用 `IsTextureFormatSupported`
- **THEN** 返回 true（该格式在 MoltenVK 上普遍支持）

### Requirement: 空桩方法与未实现路径

被砍功能的驱动方法 SHALL 以空桩实现：

- 外部图像：`CreateTextureExternalImage2` / `CreateTextureExternalImage` / `CreateTextureExternalImagePlane` / `ImportTexture` / `ImportTextureAsync` / `SetupExternalImage2` / `SetupExternalImage`
- 视频流：`CreateStreamNative` / `CreateStreamAcquired` / `SetAcquiredImage` / `SetStreamDimensions` / `GetStreamTimestamp` / `UpdateStreams` / `DestroyStream` / `SetExternalStream`
- `DispatchCompute`：与上游一致留空（上游为 `// FIXME: implement me`）
- `StartCapture` / `StopCapture`：与上游一致留空

空桩 SHALL 满足三点：
1. 签名与 `DriverAPI.inc` 声明一致
2. 不崩溃——`S` 方法返回空句柄（`Handle<...>{}`），`R` 方法空实现
3. **可识别**——以 `LOG_WARN` 或注释明确标注「未实现：外部图像 / 视频流路径已按设计砍掉」

第 3 点是硬要求：空桩若完全静默，调用者无法区分「未实现」与「已实现但无操作」。

#### Scenario: 空桩返回空句柄

- **WHEN** 调用 `CreateTextureExternalImage2S()`
- **THEN** 返回空 `TextureHandle`（`bool(handle) == false`），不崩溃

#### Scenario: 空桩可识别

- **WHEN** 调用任意外部图像或流方法
- **THEN** 输出可识别的警告信息（或源码中有明确注释标注未实现）

#### Scenario: Compute 与捕获与上游一致

- **WHEN** 调用 `DispatchCompute` / `StartCapture` / `StopCapture`
- **THEN** 空实现、无警告（与上游行为一致）

### Requirement: FeatureFlagManager 最小形态

`DriverConfig::featureFlagManager` 的类型 SHALL 从 `void const*` 收口为 `utils::FeatureFlagManager const*`。

`FeatureFlagManager` SHALL 提供最小可用形态：`IsEnabled(FeatureFlag)` 查询。若上游 Vulkan 路径仅用它判断少数开关，SHALL 提供恒返回默认值的实现并记录。

#### Scenario: 类型收口

- **WHEN** 检查 `DriverConfig::featureFlagManager` 的类型
- **THEN** 为 `utils::FeatureFlagManager const*`，不再是 `void const*`

#### Scenario: 空指针安全

- **WHEN** `featureFlagManager` 为 `nullptr` 时驱动构造
- **THEN** 不崩溃，使用默认行为

### Requirement: VulkanDriverFactory

若既有 `VulkanPlatform::CreateDriver` 直接 `new VulkanDriver`，本能力域 SHALL 保持该接线，**不引入** `VulkanDriverFactory.h`。

若上游的工厂函数承载了必要的解耦（如跨平台分支），SHALL 提供等价接线并记录理由。

#### Scenario: 接线零改动

- **WHEN** 检查 `VulkanPlatform::CreateDriver` 的实现
- **THEN** 与变更前一致（直接构造），无新增间接层

### Requirement: 适配约束
- `VulkanDriver.h` / `.cpp` SHALL 位于 `src/vulkan/`
- 受 `SharedPtr` 管理的类型参数 SHALL 用 `const Ptr&`；成员 SHALL 存 `Ptr`（避免存临时量地址）
- 命名 SHALL 遵循项目规范：公有函数 `PascalCase`、类私有函数 `camelCase`、成员 `m_camelCase`、常量 `kPascalCase`
- 上游 `VulkanDriver` 的 `camelCase` 私有方法（`collectGarbage` / `bindPipelineImpl` / `prepareDraw` / `endCommandRecording` / `acquireNextSwapchainImage` / `skipDueToEmptyRenderPass`）SHALL 保留上游拼写或改为项目规范——**决策：保留上游拼写**，理由是与上游 2956 行代码的对照成本，且它们不出现在公共接口中
- 上游文件头注释与 license 注释 SHALL 删除；`using namespace bluevk;` / `using namespace fvkutils;` SHALL 删除
- 注释 SHALL 遵循 `.dsh/rules/code-style.md`：单函数体内不超过 3 条；对非平凡逻辑（交换链 acquire 失败的处理、`bindInDraw` 延迟绑定、屏障的布局选择）说明意图；**上游注释中解释"为什么"的须按本规范重写保留，解释"做什么"的须删除**
- 本能力域 SHALL NOT 修改前 6 个变更交付的任何组件（消费者而非修改者）；若发现组件接口不足，SHALL 回到对应变更补充而非就地绕过

#### Scenario: 编译通过

- **WHEN** 构建整个 `Backend` 静态库
- **THEN** 本能力域的全部源文件编译通过，不依赖 Filament 任何头文件

#### Scenario: 命名与风格合规

- **WHEN** 检查本能力域新增的类型与函数
- **THEN** 命名遵循项目规范（`PascalCase` 类型与公有方法、`m_camelCase` 私有成员、`kPascalCase` 常量），头文件使用 `#pragma once`，不保留上游 license / 文件头注释
