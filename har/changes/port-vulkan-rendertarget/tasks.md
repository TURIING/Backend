# Tasks: port-vulkan-rendertarget

## 1. VulkanContext.cpp 差集核对与补齐

- [x] 1.1 逐行比对上游 `VulkanContext.cpp`（79 行）与既有 `src/vulkan/platform/VulkanPlatform.cpp` 的 `queryAndSetDeviceFeatures`，列出未覆盖的差集
- [x] 1.2 只补差集；若差集为空，记录「既有实现已完全覆盖」，**不创建空文件**
- [x] 1.3 复核：`VulkanContext` 每个成员的填充点只出现在一处，无重复定义

## 2. VulkanConstants

- [x] 2.1 `src/vulkan/VulkanConstants.h`：`MAX_RENDERTARGET_ATTACHMENT_TEXTURES = MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT * 2 + 1`
- [x] 2.2 上游 `VulkanConstants.h` 的其余常量（命令缓冲上限、附件上限等）逐项移植
- [x] 2.3 `BVK_DEBUG_*` 族调试开关宏集中定义；复核与既有 `BVK_ENABLED` 宏体系不冲突
- [x] 2.4 复核：`MAX_RENDERTARGET_ATTACHMENT_TEXTURES` 不在 `VulkanDriver.h` 中重复定义

## 3. VulkanFboCache

- [x] 3.1 `src/vulkan/VulkanFboCache.h`：`RenderPassKey` + `FboKey`（逐字段对齐上游）+ 各自的 Hash 与 Equal 函子
- [x] 3.2 `VulkanRenderPass` 资源对象（持 `VkRenderPass`，继承 `Resource`）+ Ptr 别名
- [x] 3.3 `VulkanFramebuffer` 资源对象（持 `VkFramebuffer`，继承 `Resource`）+ Ptr 别名
- [x] 3.4 `VulkanFboCache.h` / `.cpp`：`GetRenderPass` / `GetFramebuffer`（缓存命中返回同一对象，未命中经 `AllocateAndConstruct` 创建）
- [x] 3.5 `Gc()` / `Terminate()`
- [x] 3.6 **容器替换前置核对**：逐行核对 `Gc()` 的遍历结构，回答三个问题——(a) 是否在遍历中按 key 删除当前元素？(b) 回调是否在遍历期间修改容器？(c) 是否跨 rehash 持有迭代器或引用？
- [x] 3.7 按 3.6 的结论决定顺序：若存在 UB 模式，**先修正遍历写法，再替换容器**
- [x] 3.8 `tsl::robin_map` → `std::unordered_map`，保留自定义 Hash 与 Equal
- [x] 3.9 记录 3.6 的三个答案至 tasks 完成说明（供变更 6 的 `VulkanPipelineCache` 参照）

## 4. VulkanRenderTarget

- [x] 4.1 `src/vulkan/VulkanHandle.h`：`VulkanRenderTarget` 声明——私有继承 `HwRenderTarget` + public 继承 `Resource`
- [x] 4.2 `Auxiliary` 内部结构（`rpkey` / `fbkey` / `attachments` / `colorClearKinds` / `colors` 用 `NS_UTILS::Bitset32` / 三个 index 字段）+ `std::unique_ptr<Auxiliary> mInfo`
- [x] 4.3 `enum class ColorClearKind : uint8_t { Float, SignedInt, UnsignedInt }`
- [x] 4.4 两个构造（offscreen / default）+ 移动构造与移动赋值（经 `swap()`）
- [x] 4.5 全部查询接口（`GetExtent` / `GetSamples` / `HasDepthStencil` / `IsSwapChain` / `IsProtected` / `GetColor` / `GetColorClearKind` / `GetDepthStencil` / `GetRenderPassKey` / `GetFboKey` / `GetColorTargetCount` / `IsSwapchainBound`）
- [x] 4.6 `BindSwapChain` / `ReleaseSwapchain`
- [x] 4.7 `TransformClientRectToPlatform` / `TransformViewportToPlatform`
- [x] 4.8 `VulkanHandle.cpp`：两个构造实现（offscreen 的 `VkImage` / `VkImageView` 创建、default 的空附件）
- [x] 4.9 复核：私有继承 `HwRenderTarget` 与 `ResourceManager::Acquire<D>` 的 `HandleCast<D*, B>` 兼容性；若私有继承导致 `static_cast` 不可行，改 public 继承并记录偏离
- [x] 4.10 复核：对照上游 `VulkanDriver::beginRenderPass` 的每个 `rt->` 调用点，确认方法齐备

## 5. 渲染通道屏障

- [x] 5.1 `EmitBarriersBeginRenderPass(VulkanCommandBuffer&)`：把附件从旧布局转到 `COLOR_ATTACHMENT` / `DEPTH_STENCIL_ATTACHMENT`
- [x] 5.2 `EmitBarriersEndRenderPass(VulkanCommandBuffer&)`：转到 `SHADER_READABLE` / `DEPTH_SAMPLER`
- [x] 5.3 无附件时（未绑定交换链的 default 实例）不录制任何命令
- [x] 5.4 复核：屏障的 `srcStageMask` / `dstStageMask` / `srcAccessMask` / `dstAccessMask` 与上游逐字段比对

## 6. VulkanSwapChain

- [x] 6.1 `src/vulkan/VulkanSwapChain.h`：类声明（持颜色与深度 `VulkanTexturePtr`）+ Ptr 别名
- [x] 6.2 `src/vulkan/VulkanSwapChain.cpp`：构造——经 `VulkanPlatform::CreateSwapChain` 拿 `SwapChainBundle`，用 `VulkanTexture` 的「包装已有 `VkImage`」分支构造颜色与深度附件
- [x] 6.3 析构：释放全部附件 + 经 `VulkanPlatform::Destroy` 释放平台句柄
- [x] 6.4 `GetAttachment` / `GetExtent` / `GetSwapChainBundle` / `IsFirstRenderPass` / `MarkFirstRenderPass`
- [x] 6.5 `Acquire()`：`VulkanPlatform::Acquire` → `VulkanCommands::InjectDependency(imageAcquiredSemaphore, stage)`；失败时返回 false
- [x] 6.6 `Present()`：`VulkanCommands::AcquireFinishedSignal()` → `VulkanPlatform::Present`
- [x] 6.7 `Recreate()`：重建平台交换链与全部附件
- [x] 6.8 `HasResized()` / `IsProtected()` 转发
- [x] 6.9 复核：`Acquire` 是 `VulkanCommands` 的首次接线，确认既有 `InjectDependency` 接口签名匹配

## 7. 类型表登记

- [x] 7.1 `src/vulkan/resource/Resource.h`：`GetTypeEnum` 特化声明 ×4（`VulkanSwapChain` / `VulkanRenderTarget` / `VulkanFramebuffer` / `VulkanRenderPass`）
- [x] 7.2 `src/vulkan/resource/Resource.cpp`：4 条特化定义，分别返回 `ResourceType::SwapChain` / `RenderTarget` / `Framebuffer` / `RenderPass`
- [x] 7.3 `src/vulkan/resource/ResourceManager.cpp`：`DestroyWithType` 补 4 条分支（分支数 8 → 12）
- [x] 7.4 复核：`VulkanFramebuffer` / `VulkanRenderPass` 是内部资源（经 `AllocateAndConstruct` 创建），但仍是 `Resource` 派生、仍需特化与销毁分支

## 8. 验证

- [x] 8.1 全量构建（`cmake --build build`）无错误、无新增警告
- [x] 8.2 运行 `bin/BackendTests`：8 帧往返正常、无 `LOG_CRITICAL`、exit 0
- [x] 8.3 `tests/Engine` 新增 headless 交换链 + 渲染目标往返：构造 `VulkanSwapChain` → 构造 default `VulkanRenderTarget` → `BindSwapChain` → 查询 key → `ReleaseSwapchain` → 销毁
- [x] 8.4 断言：`GetAttachment(0, 0)` 返回有效附件；`GetExtent()` 与交换链 extent 一致；`IsSwapchainBound()` 在绑定后为 true、释放后为 false
- [x] 8.5 断言：相同 `RenderPassKey` 两次 `GetRenderPass` 返回同一句柄；相同 `FboKey` 两次 `GetFramebuffer` 返回同一句柄
- [x] 8.6 断言：`Gc()` 回收后以相同 key 再取返回**不同**句柄
- [x] 8.7 断言：`Terminate()` 路径 `vmaDestroyAllocator` 不触发 `Some allocations were not freed`
- [x] 8.8 断言：移动构造 `VulkanRenderTarget` 后源对象为空、无双重释放
- [x] 8.9 **记录局限**：`EmitBarriersBeginRenderPass` / `EmitBarriersEndRenderPass` 在 `beginRenderPass` 接线前无调用方，本变更**不验证**其运行期行为，只以编译通过 + 与上游逐字段比对为准
- [x] 8.10 **记录局限**：`Acquire` / `Present` 在无窗口环境下无法验证（需真实 surface），只验证 headless 创建/查询/销毁闭环
- [x] 8.11 回填 design 的 Open Questions：`VulkanContext.cpp` 的实际差集大小、私有继承与 `HandleAllocator` 的兼容性结论

## 实施记录

### 1. `VulkanContext.cpp` 差集核对结论（D1）

**差集为空，未创建 `src/vulkan/VulkanContext.cpp`。**

上游 `VulkanContext.cpp` 全文 79 行，**不含任何特性查询**——内容是 7 个 `VulkanAttachment` 成员函数的定义（`getImage` / `getFormat` / `getLayout` / `getExtent2D` / `getImageView` / `isDepth` / `getSubresourceRange`），外加一个空的匿名 namespace 与 `using namespace bluevk;`。

这 7 个函数在变更 4 已随 `VulkanAttachment` 落在 `src/vulkan/VulkanTexture.cpp:752-780`，逐函数语义一致（含 `getImageView` 的 `layerCount > 1 → 2D_ARRAY` 分支、`getExtent2D` 的 `width >> level` 折算）。

上游那些「上下文特性」的填充点全部在 `VulkanContext.h` / `VulkanPlatform` 侧，本项目既有 `VulkanPlatform::queryAndSetDeviceFeatures`（`src/vulkan/platform/VulkanPlatform.cpp:551-636`）已覆盖上游全部字段（`m_depthStencilFormats` / `m_blittableDepthStencilFormats` / `m_isUnifiedMemoryArchitecture` / `m_lazilyAllocatedMemorySupported` / `m_protectedMemorySupported` / `m_globalPrioritySupported` / `m_driverPropertiesSupported` / `m_pipelineCreationFeedbackSupported` / `m_vertexInputDynamicStateSupported` 等），且每个成员的填充点只出现一处。

### 2. `Gc()` 容器替换三问的答案（D4）

| 问题 | 答案 | 依据 |
|---|---|---|
| (a) 是否在遍历中按 key 删除当前元素？ | **否** | 两处都是 `iter = map.erase(iter)` 的迭代器形式（`VulkanFboCache.cpp` 的 `m_framebufferCache` / `m_renderPassCache` 两个循环），不是 `map.erase(key)`。`unordered_map` 下 `erase(const_iterator)` 返回后一元素的迭代器，合法 |
| (b) 回调是否在遍历期间修改容器？ | **否** | 遍历期间唯一的副作用是 `erase` 本身释放一个 `SharedPtr`；其 `OnLastRef()` 只把 `(type, id)` 入 `ResourceManager` 的 GC 队列，不回调进本缓存。两处额外写的是 `m_renderPassRefCount`（另一个容器），`ResetFramebuffers()` 同样只改 `m_renderPassRefCount` |
| (c) 是否跨 rehash 持有迭代器或引用？ | **否** | 循环内只取 `FboVal const fbo = iter->second;`（**值拷贝**）与 `iter->first.renderPass`（POD 拷贝），不存在跨 `erase` 存活的引用或迭代器。`m_renderPassRefCount[handle]` 的 `operator[]` 也不触碰被遍历的容器 |

**结论：不存在 UB 模式，未修改遍历写法即直接替换容器**（`tsl::robin_map` → `std::unordered_map`，保留自定义 `MurmurHashFn` 与 `*Eq` 函子）。

补充两条供变更 6 的 `VulkanPipelineCache` 参照：

- 上游 `Gc()` 的注释「Doesn't bother removing the actual map entry since it is fairly small」**已过时**——代码实际会 `erase` 掉表项。
- `ResetFramebuffers()` 是「遍历 `m_framebufferCache` 同时改 `m_renderPassRefCount`」，两者是不同的 map，故同样安全；但若日后把引用计数合并进被遍历的容器，立刻变成 UB。

### 3. `DestroyWithType` 分支数

**10 → 14**（本次 +4：`SwapChain` / `RenderTarget` / `Framebuffer` / `RenderPass`）。变更 4 批次 B 记录的「7 → 10」是本次的起点，design D7 的「8 → 12」与 spec 的「5 特化 + 4 分支」均与实际不符。

### 4. 私有继承 `HwRenderTarget` 的兼容性结论（D9 风险项）

**兼容，无需改为 public 继承。**

`HandleAllocator::HandleCast<Dp, B>` 的实现是 `static_cast<Dp>(HandleCast(handle.GetId()))`，而 `HandleCast(HandleId)` 返回 `void*`——即从 `void*` 静态向下转型，**不经过基类指针转换，因此不受继承可见性约束**。

已在 `tests/App.cpp::VerifyRenderTargetLayer` 中以驱动侧的真实调用形态验证：`ResourceManager::Acquire<VulkanRenderTarget, HwRenderTarget>(Handle<HwRenderTarget>)` 编译并运行通过（`acquireValid == true`）。`AllocateAndConstruct<VulkanRenderTarget>` / `ResourceManager::Destroy` 路径同样通过。

### 5. 与 spec / design 不一致的上游实际定义（一律以上游为准）

| # | spec / design 说 | 上游实际 | 本项目处置 |
|---|---|---|---|
| 1 | `VulkanContext.cpp`（79 行）补全 `VulkanContext` 的构造与特性填充逻辑，需与既有 `VulkanPlatform` 对接 | 该文件**没有任何特性查询**，全是 `VulkanAttachment` 的 7 个访问器定义 | 差集为空，不建文件；7 个访问器由变更 4 落在 `VulkanTexture.cpp` |
| 2 | `VulkanFboCache` 563 行 | 上游 `VulkanFboCache.{h,cpp}` 合计 **563 行？否**——.h 139 + .cpp 424 = 563，行数对得上；但 `RenderPassKey` 字段比 spec 多 `usesLazilyAllocatedMemory` 与 `viewCount`（spec 只列了 6 个字段） | 按上游补全两字段；`static_assert(sizeof(RenderPassKey) == 56)` 保留 |
| 3 | `FboKey` 有「`samples`」但未提 `renderPass` 的赋值时机 | `FboKey.renderPass` 由**驱动**在 `beginRenderPass` 里赋值（`fbkey.renderPass = renderPass->getVkRenderPass()`），`BindSwapChain` 不设置它 | 测试用例复刻该顺序，否则按 `GetFboKey()` 原样取 key 无法命中 |
| 4 | `VulkanSwapChain` 357 行，含 `Acquire` / `Present` / `Recreate` / `HasResized` / `IsProtected` / `Destroy` / `GetAttachment` / `IsFirstRenderPass` / `MarkFirstRenderPass` | 上游 `.cpp` 仅 **226 行**；**没有** `Recreate()`、`HasResized()`、`Destroy()`、`GetAttachment()`。`Recreate()` 内联在 `acquire()` 的循环里；`IsProtected()` 是 `isProtected()`；`GetAttachment` 不存在，附件由驱动的 `createSwapchainAttachment(swapchain->getCurrentColor()/getDepth())` 包装 | 按上游实现：`Acquire()` / `Present()` / `GetCurrentColor()` / `GetDepth()` / `GetExtent()` / `IsProtected()` / `IsFirstRenderPass()` / `MarkFirstRenderPass()`；销毁在析构里完成 |
| 5 | `VulkanSwapChain` 构造签名 `(VulkanContextPtr&, VulkanPlatformPtr&, VkDevice, VkQueue, void*, uint64_t, VkExtent2D)` | 上游是 `(VulkanPlatform*, VulkanContext const&, ResourceManager*, VmaAllocator, VulkanCommands*, VulkanStagePool&, void* nativeWindow, uint64_t flags, VkExtent2D)` | 按上游语义，参数改为本项目的 `Ptr` 形态 |
| 6 | `Acquire()` / `Present()` 是公开操作，`Present()` 只做信号量等待 | 上游还有 `present(DriverBase&)` 中的 frame-scheduled callback（`setFrameScheduledCallback` / `mFrameScheduled` / `PresentCallable`） | 本项目无 `FrameScheduledCallback` / `PresentCallable` 类型，且 spec 未列该能力 → **不移植**；`Present()` 去掉 `DriverBase&` 形参 |
| 7 | `MAX_RENDERTARGET_ATTACHMENT_TEXTURES` 被 `VulkanFboCache` 与 `VulkanDriver` 共同消费（design D6） | 上游 `VulkanDriver.h:57` 定义后**全文再无引用**——`VulkanFboCache` 用的是裸表达式 `MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT + ... + 1`。该常量在上游是死代码 | 常量落位 `VulkanConstants.h`（`kMaxRenderTargetAttachmentTextures`，单一来源）；`VulkanFboCache` 保留上游的裸表达式写法 |
| 8 | spec 的 `VulkanConstants.h` 要求「`BVK_DEBUG_*` 族调试开关宏集中定义」 | 上游 `VulkanConstants.h` 里是 `FVK_DEBUG_*`；本项目在变更 2 已把整套开关（含 `BVK_ENABLED` / `BVK_DEBUG_FLAGS` / `BACKEND_DEBUG_FLAG` 转发）落在 `src/vulkan/VkDef.h` | **不重复定义**，`VulkanConstants.h` 只放尚未落位的常量（`kMaxRenderTargetAttachmentTextures` / `kMaxPipelineAge` / `kRenderdocCaptureMode`）；`kMaxCommandBuffers`、`kRequiredVulkanVersion*`、`kVkAlloc` 已在 `VkDef.h` |
| 9 | （同上）上游 `VulkanConstants.h` 的 SYSTRACE / `FVK_LOGx` / `FVK_HANDLE_ARENA_SIZE_IN_MB` 等 | SYSTRACE 依赖 Filament `private/utils/Tracing.h`；`FVK_LOGx` 由本项目 `Utils/Log.h` 取代；handle arena 尺寸本项目已有 `VulkanDriver.cpp` 的 `kMinHandleArenaSize` | 均不移植，理由如上 |
| 10 | spec 未提 `VulkanRenderPassContext` | 上游定义在 `VulkanContext.h:61`，`GetColorTargetCount(VulkanRenderPassContext const&)` 依赖它；含 `commandBuffer` / `renderTarget` / `renderPass` / `params` / `currentSubpass` | **新增**（spec 漏项）：落在 `src/vulkan/VulkanContext.h`，与上游同址 |
| 11 | spec 未提 `HwRenderTarget` / `HwSwapChain` | 两者在上游均在 `DriverBase.h`；本项目 `src/HwDefine.h` 此前只有 `HwTexture` 等（变更 4 补的） | **新增**（spec 漏项）：按上游字段补入 `src/HwDefine.h` |
| 12 | spec 未提 `isUnsignedIntFormat` / `isSignedIntFormat` | 上游在 `backend/DriverEnums.h`，被 `VulkanRenderTarget` 构造用于填 `ColorClearKind` | **新增**（spec 漏项）：落在本项目对应文件 `include/Backend/DriverDefine.h`，命名改为 `IsUnsignedIntFormat` / `IsSignedIntFormat` |
| 13 | spec：`EmitBarriersEndRenderPass` 转到 `SHADER_READABLE` | `VulkanLayout` **没有** `SHADER_READABLE`（批次 A 已记录）；上游用的是 `VulkanLayout::FRAG_READ`，深度用 `DEPTH_SAMPLER` | 按上游用 `FRAG_READ` / `DEPTH_SAMPLER` |
| 14 | design D7 表：`DestroyWithType` 分支数 8 → 12 | 实际起点是 10（批次 B 的 `StageImage` / `Texture` / `TextureState`），本次 → **14** | 以实际为准 |
| 15 | design D3 称 `Auxiliary` 里是 `utils::bitset32 colors` | 上游是 `utils::bitset32`，本项目映射为 `NS_UTILS::Bitset32`（变更 1 约定），API 为 `Set` / `Count` / `ForEachSetBit` / `operator[]` | 按本项目类型实现 |
| 16 | 上游 `VulkanFboCache::Gc()` 头注释「Doesn't bother removing the actual map entry」 | 代码实际会 `erase` 掉表项 | 不照搬该过时注释 |
| 17 | 上游 `VulkanFboCache` 的 `#if FVK_ENABLED(FVK_DEBUG_FBO_CACHE)` 调试日志 | 该块引用 `config.depth` / `config.depthFormat`——**键里根本没有这两个字段**，开启该宏即无法编译 | 本项目改写为只打印键中真实存在的字段（句柄以裸地址格式化会绑定平台指针宽度，故不打句柄） |
| 18 | 上游 `VulkanRenderTarget::emitBarriersBeginRenderPass` 的布局查询 | `tex->getLayout(range.baseMipLevel, range.baseArrayLayer)`，但签名是 `getLayout(uint32_t layer, uint32_t level)`——**参数颠倒**（layer=0 时无差异，分层/立方体贴图附件会漏屏障） | 按签名语义修正为 `GetLayout(range.baseArrayLayer, range.baseMipLevel)`，并在本表记录 |
| 19 | 上游 `VulkanSwapChain` 用 `mCommands->flush()/wait()/get()` 且无空指针保护 | 本项目驱动侧 (变更 7) 尚未持有 `VulkanCommands`，测试路径只能传空 | `m_commands == nullptr` 时跳过命令录制/提交路径（与变更 4 的 `VulkanStagePool` 第 4 参 `nullptr` 处置一致） |
| 20 | 命名：types/methods `PascalCase`、常量 `kPascalCase`、成员 `m_camelCase` | 上游为 `getRenderPass` / `renderPassCache` / `FINAL_COLOR_ATTACHMENT_LAYOUT` 等 | 全部按本项目规范重命名；`VulkanFboCache::kFinal*AttachmentLayout` 取代上游 `FINAL_*_LAYOUT` |

### 6. 移植取舍（非 spec 差异，但影响行为强度）

上游的 `FILAMENT_CHECK_POSTCONDITION` / `FILAMENT_CHECK_PRECONDITION` 是**始终生效**的检查（release 也会 abort 并打印），本项目对应位置改用 `LOG_ASSERT`（release 下为 no-op）。涉及：

- `~VulkanFboCache` 的「必须先 Terminate」后置条件
- `vkCreateFramebuffer` / `vkCreateRenderPass` / `VulkanPlatform::CreateSwapChain` 的返回值校验
- `VulkanSwapChain::Present` 的 `vkQueuePresentKHR` 结果校验
- `VulkanSwapChain::GetCurrentColor` 的图像下标前置条件

若后续要求 release 下也兜底，应把这些换成 `LOG_CRITICAL`（本项目 abort 语义）——本变更保持与既有代码一致的 `LOG_ASSERT` 风格。

### 7. 运行期验证与负向对照

`tests/App.cpp::VerifyRenderTargetLayer`（在既有 `VerifyHeadlessSwapChain` / `VerifyTextureLayer` 之后执行）：

- headless 交换链（1280×720）：颜色/深度附件均有效、`GetExtent2D()` 与构造尺寸一致、`IsFirstRenderPass()` → `MarkFirstRenderPass()` 后转 false
- default `VulkanRenderTarget`：未绑定时 `IsSwapChain()==true` / `IsSwapchainBound()==false` / `HasDepthStencil()==false`
- `BindSwapChain`：`GetExtent()` 与交换链一致、`GetSamples()==1`、`GetRenderPassKey().colorFormat[0]` 与颜色纹理格式一致、`GetFboKey()` 的颜色/深度视图非空
- `ResourceManager::Acquire<VulkanRenderTarget, HwRenderTarget>` 取回同一对象（私有继承兼容性实证）
- `VulkanFboCache`：同 `RenderPassKey` 两次 `GetRenderPass` 返回同一句柄；同 `FboKey` 两次 `GetFramebuffer` 返回同一句柄
- `Gc()` 逐出（`ResetFramebuffers()` + 64 轮 `Gc()`）后同 key 再取返回**不同**句柄（render pass 与 framebuffer 各自校验）
- `ReleaseSwapchain()` 后 `IsSwapchainBound()==false`
- 重新绑定后移动构造：目标持有全部状态、源对象 `GetExtent()` 退化为 0（`mInfo` 已交出）、作用域结束无双重释放
- `vmaDestroyAllocator` 不触发 `Some allocations were not freed`（`allocationCount == 0`）

**负向对照（各跑一次后还原，证明断言真的会失败）**：

1. 关掉 `GetFramebuffer` 的缓存查找 → `cacheHit=false` → `LOG_CRITICAL` → **exit 134**
2. 让 `Gc()` 直接 return（不逐出）→ `cacheEvict=false` → `LOG_CRITICAL` → **exit 134**

### 8. 未验证面（局限）

- `EmitBarriersBeginRenderPass` / `EmitBarriersEndRenderPass` 在变更 7 接线 `beginRenderPass` 前无调用方，仅以编译通过与上游逐字段比对为准
- `Acquire()` / `Present()` 需要真实 surface，headless 环境不可验证；`VulkanCommands` 的空指针分支因此也未运行
- `VulkanRenderTarget` 的 offscreen 构造（含 MSAA 侧车）本变更无调用方，属变更 7 的 `createRenderTargetR`，未运行期验证；已按上游逐行移植
