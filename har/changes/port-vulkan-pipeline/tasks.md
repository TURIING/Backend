# Tasks: port-vulkan-pipeline

> 本变更是体量最大的一块（约 3200 行）。任务按组件分组，每组产出的文件应可独立编译通过后再进入下一组。

## 1. VulkanAsyncHandles

- [x] 1.1 `src/vulkan/VulkanAsyncHandles.h`：`PushConstantDescription` 声明（从 `backend::Program` 构建 + `Write`）
- [x] 1.2 `VulkanProgram` 声明：`HwProgram` + `ThreadSafeResource`；成员 `VkShaderModule mShaders[2]` / `PushConstantDescription` / `std::atomic<bool> mParallelCompilationCanceled` / `std::vector<PushConstantInfo> mQueuedPushConstants`
- [x] 1.3 `VulkanFence` / `VulkanSync` / `VulkanTimerQuery` 声明（均 `ThreadSafeResource`；`utils::Mutex` → `std::mutex`）
- [x] 1.4 `VulkanAsyncHandles.h` SHALL include 既有 `vulkan/sync/VulkanCmdFence.h`，**SHALL NOT** 重复定义 `VulkanCmdFence`
- [x] 1.5 `src/vulkan/VulkanAsyncHandles.cpp`：`PushConstantDescription` 实现（`GetVkStage` 辅助 + 范围构建 + `Write`）
- [x] 1.6 `VulkanAsyncHandles.cpp`：`VulkanProgram` 构造（创建 `VkShaderModule`）/ 析构（销毁）/ `FlushPushConstants` / `WritePushConstant`
- [x] 1.7 复核：全项目检索 `struct VulkanCmdFence`，确认只有 `src/vulkan/sync/VulkanCmdFence.h` 一处
- [x] 1.8 复核：四个类型的继承列表均含 `ThreadSafeResource`，且 `isThreadSafeType` 覆盖对应的四个 `ResourceType`

## 2. VulkanRenderPrimitive

- [x] 2.1 `src/vulkan/VulkanHandle.h` / `.cpp`：`VulkanRenderPrimitive`（`HwRenderPrimitive` + `Resource`；持 `VulkanVertexBufferPtr` / `VulkanIndexBufferPtr` / `VkPrimitiveType type`）
- [x] 2.2 复核：字段名与类型对照上游 `VulkanDriver::draw` 的读取点（`rp->type` / `rp->vertexBuffer->vbi`）

## 3. VulkanPipelineLayoutCache 与 VulkanDescriptorSetLayoutCache

- [x] 3.1 `VulkanDescriptorSetLayoutCache.{h,cpp}`：`VulkanDescriptorSetLayout` 资源对象（含 `bitmask` 与 `externalSampler`）+ `CreateLayout` + `GetVkLayout` + `Terminate`
- [x] 3.2 `VulkanPipelineLayoutCache.{h,cpp}`：`DescriptorSetLayoutArray` + `GetLayout(vkLayouts, program)` + `Terminate`
- [x] 3.3 容器替换为 `std::unordered_map`（两者均为只增缓存，无 `erase`，风险低）
- [x] 3.4 复核：`layout->bitmask.externalSampler.count() > 0` 的表达式可用（`SamplerBitmask` 需有 `count()`）

## 4. VulkanPipelineCache

- [x] 4.1 `VulkanPipelineCache.h`：嵌套 `struct RasterState`，**逐字段对齐上游（约 20 字段）**
- [x] 4.2 `VulkanPipelineCache.h`：`PipelineKey` / `PipelineEqual` / `PipelineHashFn` + 缓存容器声明
- [x] 4.3 `VulkanPipelineCache.h`：全部绑定接口 + `GetOrCreatePipeline` + `AsyncPrewarmCache` + `AddCachePrewarmCallback` + `ResetBoundPipeline` / `Gc` / `Terminate` + `CompilerThreadPool` / `CallbackManager` 成员
- [x] 4.4 **容器替换前置核对**：逐行核对 `Gc()` 与 `ResetBoundPipeline()` 的遍历结构，回答三问——(a) 遍历中按 key 删除？(b) 回调期间修改容器？(c) 跨 rehash 持有迭代器/引用？
- [x] 4.5 按 4.4 结论决定顺序：若存在 UB 模式，**先修正遍历写法，再替换容器**
- [x] 4.6 `VulkanPipelineCache.cpp`：绑定接口实现（纯状态更新，无 VK 调用）
- [x] 4.7 `VulkanPipelineCache.cpp`：`CreatePipeline`（组装 `VkGraphicsPipelineCreateInfo`，约 240 行）
- [x] 4.8 `VulkanPipelineCache.cpp`：`Cursor` 与 `PipelineEqual::operator()`（约 60 行的管线缓存相等性比较）
- [x] 4.9 `VulkanPipelineCache.cpp`：`AsyncPrewarmCache` + `AddCachePrewarmCallback` + `Terminate`
- [x] 4.10 `VulkanPipelineCache.cpp`：`utils::JobSystem::setThreadName` / `setThreadPriority` → `NS_BD::JobSystem::SetThreadName` / `SetThreadPriority`
- [x] 4.11 `tsl::robin_map` → `std::unordered_map`，保留 `PipelineHashFn` / `PipelineEqual`
- [x] 4.12 复核：`RasterState` 逐字段对照上游 `VulkanDriver::bindPipelineImpl` 的指定初始化器（20 个字段全部核对）
- [x] 4.13 记录 4.4 的三问答案 + 4.11 后的查找性能实测（哪怕定性结论）

## 5. VulkanDescriptorSetCache

- [x] 5.1 `VulkanDescriptorSetCache.h`：`VulkanDescriptorSet` 资源对象（`boundLayout` / `isLayoutDirty` / `isAnExternalSamplerBound` / `uniqueDynamicUboCount` / `getVkSet()` / `getOffsets()`）
- [x] 5.2 `VulkanDescriptorSetCache.h` / `.cpp`：`Bind` / `Unbind` / `GetBoundSets` / `Commit` / `Terminate`
- [x] 5.3 `Commit` 的三段逻辑：掩码与 stash 求交 → 跳过与上次相同的集合 → 对剩余位 `vkCmdBindDescriptorSets` + `commands->Acquire(set)`
- [x] 5.4 容器替换为 `std::unordered_map`；**核对 `Commit` 中是否有容器修改**（`DescriptorSetMask::ForEachSetBit` 遍历期间）
- [x] 5.5 复核：`DescriptorSetOffsetArray` 的来源（变更 2 的 `DriverDefine.h` 是否已提供；若否则在此补其类型定义并回填 `backend-enums` spec）

## 6. VulkanQueryManager

- [x] 6.1 `VulkanQueryManager.{h,cpp}`：构造（`timestampPeriod` 判定）+ `AcquireTimerQuery` / `ReleaseTimerQuery` + 空闲池与 `VkQueryPool` 扩容
- [x] 6.2 `BeginTimerQuery` / `EndTimerQuery`（录制 `vkCmdWriteTimestamp`）
- [x] 6.3 `GetTimerQueryValue`：先查 `IsCompleted()`，未完成返回 `NOT_READY`；完成后按 `timestampPeriod` 换算纳秒
- [x] 6.4 `Reset` / `Terminate`
- [x] 6.5 复核：`VulkanTimerQuery` 的 `startingIndex` / `stoppingIndex` 分配与回收无重叠

## 7. VulkanBlitter

- [x] 7.1 `VulkanBlitter.{h,cpp}`：`Blit`（`vkCmdBlitImage` + 布局转换）
- [x] 7.2 `Resolve`（`vkCmdResolveImage`）
- [x] 7.3 深度/模板格式的能力判定与 `vkCmdCopyImage` 降级路径
- [x] 7.4 `Terminate`

## 8. VulkanReadPixels

- [x] 8.1 `VulkanReadPixels.{h,cpp}`：读回线程（`std::thread` + `std::condition_variable` + `std::queue`）+ 构造/`Terminate`（join）
- [x] 8.2 `ReadPixels` / `ReadTexture` / `ReadBufferSubData` 实现（`vkCmdCopyImageToBuffer` → map → 拷贝 → unmap → 回调）
- [x] 8.3 以注释写明生命周期约束：读回线程须在 `VmaAllocator` 销毁前终止
- [x] 8.4 复核：暂存资源经 `VulkanStagePool` 获取，不裸持 `VmaAllocator` 跨线程使用

## 9. 类型表登记

- [x] 9.1 `Resource.h` / `Resource.cpp`：`GetTypeEnum` 特化 ×6（`VulkanProgram` / `VulkanFence` / `VulkanSync` / `VulkanTimerQuery` / `VulkanDescriptorSetLayout` / `VulkanDescriptorSet`）
- [x] 9.2 `Resource.h` / `Resource.cpp`：`GetTypeEnum<VulkanRenderPrimitive>` 特化
- [x] 9.3 `ResourceManager.cpp`：`DestroyWithType` 补对应分支（分支数 12 → 18）
- [x] 9.4 复核：`isThreadSafeType` 覆盖的四个类型在本变更全部落地，且构造路径走 `ThreadSafeResource::Init`

## 10. 验证

- [x] 10.1 全量构建（`cmake --build build`）无错误、无新增警告
- [x] 10.2 运行 `bin/BackendTests`：8 帧往返正常、无 `LOG_CRITICAL`、exit 0
- [x] 10.3 **`ThreadSafeResource` 路径验证（兑现变更 1 的待验证项）**：构造 `VulkanProgram` → 释放引用 → `Gc()` → 断言对象析构 + 池块归还 + 走的是 `m_threadSafeGcList`
- [x] 10.4 单元测试：`VulkanProgram` 以最小 SPIR-V 构造后 `GetVertexShader()` / `GetFragmentShader()` 非空，析构后无 `VkShaderModule` 泄漏
- [x] 10.5 单元测试：管线缓存相同 `PipelineKey` 两次 `GetOrCreatePipeline` 返回同一 `VkPipeline`；不同 key 返回不同；`Gc()` 后旧项回收
- [x] 10.6 单元测试：`VulkanPipelineLayoutCache` 相同布局命中、push constant 范围参与键
- [x] 10.7 单元测试：`VulkanQueryManager` `AcquireTimerQuery` → `ReleaseTimerQuery` → 再次获取复用同一查询；`Terminate` 后无泄漏
- [x] 10.8 单元测试：`VulkanReadPixels` 构造后立即析构不挂起
- [x] 10.9 断言：`Terminate()` 路径 `vmaDestroyAllocator` 不触发 `Some allocations were not freed`
- [x] 10.10 **记录局限**：`VulkanBlitter::Blit` / `Resolve` 需要真实命令录制与渲染目标，`beginRenderPass` 接线前**不验证**运行期行为
- [x] 10.11 **记录局限**：`VulkanReadPixels` 的实际读回需要渲染目标，本变更**只验证构造/析构**
- [x] 10.12 **记录局限**：并行编译路径的并发入队（编译线程归零 + backend 线程 `Gc()`）无法在无真实管线编译的测试中触发；以代码走查 + 变更 1 的锁范围复核结论为准，不得宣称已并发验证
- [x] 10.13 回填 design 的 Open Questions：`PipelineHashFn` 的哈希等价性、`DescriptorSetCache::Commit` 的遍历期间容器修改结论

## 实施记录

### 1. 文件清单与行数

新增（`src/vulkan/`）：

| 文件 | .h | .cpp |
|---|---|---|
| `VulkanAsyncHandles` | 174 | 147 |
| `VulkanPipelineCache` | 230 | 424 |
| `VulkanPipelineLayoutCache` | 74 | 70 |
| `VulkanDescriptorSetLayoutCache` | 67 | 148 |
| `VulkanDescriptorSetCache` | 88 | 391 |
| `VulkanQueryManager` | 62 | 95 |
| `VulkanBlitter` | 32 | 159 |
| `VulkanReadPixels` | 89 | 451 |
| 合计 | 816 | 1885 |

修改：`src/HwDefine.h`（+7 个 `Hw*` 类型）、`include/Backend/DriverDefine.h`（`DescriptorSetOffsetArray`）、
`src/vulkan/VulkanContext.h`（预编译开关访问器）、`src/vulkan/VulkanHandle.{h,cpp}`（`VulkanRenderPrimitive` /
`VulkanDescriptorSetLayout` / `VulkanDescriptorSet`）、`src/vulkan/resource/Resource.{h,cpp}`（+7 条特化、
`GetTypeEnum` 补 `const`）、`src/vulkan/resource/ResourceManager.{h,cpp}`（+7 条销毁分支、两条队列长度访问器）、
`src/vulkan/stage/VulkanStageBuffer.{h,cpp}` 与 `StagePool.{h,cpp}`（`GetTypeEnum` 特化同步补 `const`）、
`tests/Engine.{h,cpp}`（`GetDriverBase()`）、`tests/App.cpp`（`VerifyPipelineLayer` 及其 7 个子用例）。

### 2. 构建与测试

- `cmake --build build`：0 error / 0 warning（`-Wnonportable-include-path` 除外；`Backend` 目标全量重建后仍为 0）
- `bin/UtilsTests`：154 tests passed，exit 0
- `bin/BackendTests`：exit 0，新增的 `VerifyPipelineLayer` 全部子项通过

### 3. 运行期验证（`tests/App.cpp::VerifyPipelineLayer`）

| 子用例 | 断言 |
|---|---|
| `VerifyThreadSafeProgramPath` | `VulkanProgram` 以最小 SPIR-V 构造后 `GetVertexShader()` / `GetFragmentShader()` 均非空、push constant 范围数为 1、取消标记可置可读；经 `ResourceManager::Destroy` 归零后 **`m_threadSafeGcList` 长度为 1 且 `m_gcList` 为 0**；`Gc()` 后两条队列均空；同尺寸再分配拿到**同一池块地址**（即析构 + 池块归还确实发生） |
| `VerifyPipelineLayoutCache` | 相同布局数组 + 同一 program 两次 `GetLayout` 返回同一 `VkPipelineLayout`；push constant 的 stage 不同则返回不同的布局 |
| `VerifyDescriptorSetLayoutCache` | `CreateLayout` 产出的布局含有效 `VkDescriptorSetLayout`、`count.ubo == 1`、无外部采样器；同掩码 `GetVkLayout` 命中同一句柄 |
| `VerifyDescriptorSetCache` | `CreateSet` 产出有效 `VkDescriptorSet` 且 `uniqueDynamicUboCount == 0`；`Bind` → `GetBoundSets()` 取回同一对象、偏移数组长度为 2；`Unbind` 后位清空；`Terminate` 后再销毁集合不触碰已释放的池 |
| `VerifyPipelineCache` | 相同状态两次 `GetOrCreatePipeline()` 返回同一 `VkPipeline`；仅 `cullMode` 不同返回不同句柄；`kMaxPipelineAge + 4` 轮 `Gc()` 后缓存条目数由 2 归 0；同状态再取重建成功且条目数回到 1 |
| `VerifyQueryManager` | `GetNextQuery` 的起止下标相邻；`ClearQuery` 后再取复用同一起始下标；连续取满 32 个后返回空而非越界；`Terminate` 幂等 |
| `VerifyReadPixels` | 构造 → `Terminate`（重复调用幂等）→ `RunUntilComplete` 全程 0ms 不挂起；`TaskHandler` 的 `Post`/`Drain`/`Shutdown` 真实起线程、join，且 `Shutdown` 后剩余任务仍触发完成回调 |
| 泄漏 | `resourceManager->Terminate()` 后 `vmaCalculateStatistics` 的 `allocationCount == 0`，`vmaDestroyAllocator` 未触发 `Some allocations were not freed` |

**负向对照**（各跑一次后还原，证明断言真的会失败）：

1. 把 `IsThreadSafeType` 的 `ResourceType::Program` 改为 `false` → `threadSafeQueue=false` → `LOG_CRITICAL` → **exit 134**
2. 让 `VulkanPipelineCache::Gc()` 直接 return（不逐出）→ `gcEmptied=false`、`rebuilt=false` → `LOG_CRITICAL` → **exit 134**

### 4. `Gc()` 容器替换三问的答案（design D2 / task 4.4）

| 问题 | 答案 | 依据 |
|---|---|---|
| (a) 是否在遍历中按 key 删除当前元素？ | **否** | 上游用的是 `iter = m_pipelines.erase(iter)` 的迭代器形式，不是 `erase(key)`。`std::unordered_map::erase(const_iterator)` 返回后一元素的迭代器，合法 |
| (b) 回调是否在遍历期间修改容器？ | **否** | 遍历期间唯一副作用是 `vkDestroyPipeline`（不触碰 `m_pipelines`）；`ResetBoundPipeline()` 在循环之前调用，只改 `m_boundPipeline`；本函数不派发任何用户回调 |
| (c) 是否跨 rehash 持有迭代器或引用？ | **否** | 循环内 `PipelineCacheEntry const& cacheEntry = iter->second` 仅在本次迭代内使用，`erase` 之后不再触碰；`GetOrCreatePipeline()` 返回指向 map 节点的指针，调用方（`BindPipeline`）立即读取 `->handle` 后即丢弃。换成 node-based 的 `std::unordered_map` 后指针/引用跨 rehash 稳定，反而比 `robin_map` 更安全 |

**结论：不存在 UB 模式，未修改遍历写法即直接替换容器**（`tsl::robin_map` → `std::unordered_map`，保留 `PipelineHashFn` 与 `PipelineEqual`）。
另记四条：

- 上游 `Gc()` 的注释「NOTE: Due to robin_map restrictions, we cannot use auto or range-based loops」随容器替换失效，已删除；
- `ResetBoundPipeline()` 不涉及任何容器遍历，无需核对；
- `VulkanDescriptorSetLayoutCache` / `VulkanPipelineLayoutCache` 都是只增缓存，无 `erase`，风险同变更 5 的采样器缓存；
- `VulkanDescriptorSetCache` 的 `Commit` 遍历的是栈上位掩码拷贝，见下节。

### 5. `DestroyWithType` 分支数

**14 → 21**（本次 +7：`Program` / `Fence` / `Sync` / `TimerQuery` / `DescriptorSetLayout` / `DescriptorSet` / `RenderPrimitive`）。
design 与 spec 写的「12 → 18」「+6 条」均与实际不符：起点是变更 5 记录的实际值 14，
且本变更引入的资源类型是 **7** 个而非 6 个（`VulkanRenderPrimitive` 与 `VulkanProgram` 都走 `Make`，
`DescriptorSetLayout` / `DescriptorSet` 也各自需要句柄背书的 `Make`）。

### 6. `ThreadSafeResource` 路径是否真的走通

**走通了，且有直接证据。** `Resource::OnLastRef` 只拿得到运行期 `m_type`，`ResourceManager::destructLaterWithType` 据此
`IsThreadSafeType(type)` 分流。运行期断言 `GetPendingThreadSafeGcCount() == 1 && GetPendingGcCount() == 0` 正是
「`VulkanProgram` 归零进了 `m_threadSafeGcList`」的直接观测；负向对照 1 证明该断言对分类错误敏感。
`VulkanFence` / `VulkanSync` / `VulkanTimerQuery` 的分类走同一条 `IsThreadSafeType` 分支，
但本变更没有它们的运行时归零用例（`VulkanTimerQuery` 由 `VulkanQueryManager` 经 `AllocateAndConstruct` 创建、
以 `Reset()` 归还，未经过 `Destroy`；`VulkanFence` / `VulkanSync` 的创建属变更 7），故**这四个类型中只有
`VulkanProgram` 得到运行期验证**——分类表本身对四者的覆盖已静态核对（tasks 1.8）。

### 7. 与 spec / design 不一致的上游实际定义（一律以上游为准）

| # | spec / design 说 | 上游实际 | 本项目处置 |
|---|---|---|---|
| 1 | `VulkanPipelineCache` 825 行 | `.h` 276 + `.cpp` **549** = 825，行数对得上；但 `.cpp` 里**没有**「`Cursor` 与 `PipelineEqual::operator()` 约 60 行」这段——`PipelineEqual` 只有 3 行 memcmp，`Cursor` 类型在上游**不存在** | 不实现 `Cursor`；`PipelineEqual` 按上游 3 行实现 |
| 2 | `VulkanDescriptorSetCache` 584 行 | `.h` 120 + `.cpp` **464** = 584；但 `.h` 里的 `updateSamplerForExternalSamplerSet` **没有定义**（唯一调用方是未移植的 `VulkanExternalImageManager`），`mInputAttachment` 成员无人读写 | 不移植该声明与成员（保留 `UpdateInputAttachment` 空实现，与上游同为空） |
| 3 | `VulkanQueryManager(VkDevice, uint32_t queueFamilyIndex, VkPhysicalDeviceLimits const&)`，按 `timestampPeriod` 判定支持；方法为 `AcquireTimerQuery` / `ReleaseTimerQuery` / `BeginTimerQuery` / `EndTimerQuery` / `GetTimerQueryValue → TimerQueryResult` / `Reset()` | 构造只有 `(VkDevice)`；方法是 `getNextQuery(ResourceManager*)` / `clearQuery` / `beginQuery` / `endQuery` / `getResult → QueryResult` / `terminate()`。**没有** `timestampPeriod` 参数、**没有** `TimerQueryResult` 枚举、**没有** `Reset()`；`timestampPeriod` 换算在 `VulkanDriver::getTimerQueryValue` 里做 | 按上游签名与命名（`GetNextQuery` / `ClearQuery` / `BeginQuery` / `EndQuery` / `GetResult` / `Terminate`）；`QueryResult` 的 `beginAvailable` / `endAvailable` 即 spec 想要的「就绪判定」 |
| 4 | 查询池 SHALL 在达到 `VkQueryPool` 容量时新建池 | 上游**不扩容**：`~mUsed` 为空时 `LOG_ERROR` 并返回空句柄 | 按上游实现；测试额外覆盖了「取满 32 个后返回空而非越界」 |
| 5 | `VulkanBlitter::Blit(VulkanCommandBuffer*, VulkanTexture const& src, VkImageBlit const&, VulkanTexture& dst, ...)` / `Resolve(VulkanCommandBuffer*, ...)` / `IsDepthStencilBlitSupported` / 深度格式降级为 `vkCmdCopyImage` | 实际是 `blit(VkFilter, VulkanAttachment dst, VkOffset3D const* dstRectPair, VulkanAttachment src, VkOffset3D const* srcRectPair)` 与 `resolve(VulkanAttachment dst, VulkanAttachment src)`；附件**按值**传递；`IsDepthStencilBlitSupported` 与 `vkCmdCopyImage` 降级路径**都不存在**（深度解析直接 `assert_invariant` 拒绝） | 按上游实现；`Blit` / `Resolve` 用 PascalCase |
| 6 | `VulkanReadPixels`：`ReadPixels` / `ReadTexture` / `ReadBufferSubData` / `Terminate`；读回经 `vmaMapMemory` / `vmaUnmapMemory` 中转；暂存资源经 `VulkanStagePool` 获取 | 方法只有两个 `run(...)` 重载 + `runUntilComplete()` + `terminate()`；**完全没有 VMA**——用 `vkCreateBuffer` + `vkAllocateMemory` + `vkMapMemory` 自建暂存缓冲（上游注释明说「池在另一个线程上，要用得先让池线程安全」） | 按上游实现；`Run` / `RunUntilComplete` / `Terminate` |
| 7 | 「读回线程须在 `VmaAllocator` 销毁前终止」 | 该约束的**对象错了**：读回线程持有的是 `VkDevice` 与自建 `VkCommandPool`，与 `VmaAllocator` 无关 | 注释改写为「须在 `VkDevice` 销毁前终止」，并顺带修正上游 `terminate()` 的销毁顺序（见第 8 项） |
| 8 | （同上） | 上游 `terminate()` 先 `vkDestroyCommandPool` 再 `mTaskHandler->shutdown()`：若当时有任务在途，工作线程会使用已销毁的命令池 | **已修正为「先 `Shutdown()`（join）再销毁命令池」**，并在注释中说明理由 |
| 9 | `VulkanRenderPrimitive` 持 `VkPrimitiveType type` | 类型字段来自 `HwRenderPrimitive`，是后端枚举 `PrimitiveType`（`VulkanDriver::draw` 写的是 `state.primitiveType = rp->type`），**不是** `VkPrimitiveType`；`VulkanRenderPrimitive` 自身只多两个指针成员 | 按上游：`HwRenderPrimitive::type` 承继，类内只放 `vertexBuffer` / `indexBuffer` |
| 10 | `ResourceManager::construct` 的 `if constexpr` 分派到 `ThreadSafeResource::Init<D>` | 本项目 `ThreadSafeResource` 是空标记结构，`construct` 只调 `init<D>(id, this)`；分类发生在归零侧（`OnLastRef` → `IsThreadSafeType`） | 按本项目既有实现，未改 |
| 11 | `GetTypeEnum` 特化 ×6 与销毁分支 ×6（12 → 18） | 本变更引入的类型是 7 个；实际分支数 14 → **21** | 以实际为准 |
| 12 | `DescriptorSetMask` 方法名 `ForEachSetBit` / `Set` / `Unset` / `Test` / `Count` / `Any` / `None` / `All` / `FirstSetBit` 可用 | 本项目 `NS_UTILS::Bitset8` 提供的正是这套 PascalCase 接口；上游是小写 `forEachSetBit` / `unset` / `test` / `count` / `firstSetBit` | 按本项目接口实现 |
| 13 | `DescriptorSetOffsetArray` 若 `DriverDefine.h` 没有则补 | `DriverDefine.h` 确实没有；上游该类型是**命令流 arena 的非拥有视图**（构造需要 `DriverApi&`） | **补在 `DriverDefine.h`，但实现为按值拥有 `std::vector<uint32_t>`**：本项目命令流尚无该分配接口，且该数组会被 `VulkanDescriptorSet` 移动留存到 `Commit` 才读取，非拥有视图会悬垂。属有意偏离，已在代码注释中写明 |
| 14 | `VulkanDescriptorSetLayout` 含 `bitmask`（`DescriptorSetMask`）与 `externalSampler`（`SamplerBitmask`）成员；`layout->bitmask.externalSampler.count() > 0` | `bitmask` 是**一个结构体** `Bitmask{ubo, dynamicUbo, sampler, inputAttachment, externalSampler}`（40 字节），`externalSampler` 是它的字段而非 `bitmask` 本身的类型；`count()` 在本项目写作 `Count()` | 按上游结构实现；另提供 `HasExternalSamplers()` 便于调用点 |
| 15 | `VulkanDescriptorSet` 含 `getVkSet()` / `getOffsets()` | 还有 `setOffsets` / `acquire(Resource)` / `referencedBy` / `isBound` / `getLayout` / `boundLayout` / `isLayoutDirty` / `isAnExternalSamplerBound` / `dynamicUboMask` / `uniqueDynamicUboCount`；私有 `addNewSet` / `gc` / `InternalVkSet` 含 `VkDescriptorSet` + 回收回调 + `shared_ptr<VulkanCmdFence>` | 全部按上游移植 |
| 16 | `VulkanDescriptorSetCache::Commit` 先「掩码与 stash 求交」再「跳过与上次相同的集合」 | 与上游一致，但求交用的是 `if (!updateSets[index]) curMask.Unset(index)`，跳过用的是 `set == lastBoundSets[index] && set->uniqueDynamicUboCount == 0`——**且整段以 `mLastBoundInfo.pipelineLayout == pipelineLayout` 为前提**（布局换了就不能跳过） | 按上游实现；`SharedPtr` 无 `operator==`，改为比较 `Get()` |
| 17 | `VulkanDescriptorSetCache` 只提 `Bind` / `Unbind` / `GetBoundSets` / `Commit` / `Terminate` | 还有 `UpdateBuffer` / `UpdateSampler` / `UpdateInputAttachment` / `CreateSet` / `CloneSet` / `GetVkSet` / `ManualRecycle` / `Gc` / `ResetCachedState`，以及私有的 `DescriptorInfinitePool` + `DescriptorPool`（约 200 行的无限池） | 全部按上游移植——变更 7 的 `createDescriptorSetR` / `updateDescriptorSet*` 直接依赖 |
| 18 | `VulkanPipelineLayoutCache` 的 `DescriptorSetLayoutArray` | 该别名定义在 `VulkanDescriptorSetLayout` 内部（`std::array<VkDescriptorSetLayout, 4>`），缓存只做 type alias 转发 | 按上游：别名的权威定义在 `VulkanHandle.h` 的 `VulkanDescriptorSetLayout` |
| 19 | `VulkanPipelineLayoutCache::GetLayout` 的键含 push constant 范围 | 键只含 `{stage, size}` 两项——**不含 offset**（上游注释：我们假定更新范围从 0 起），且数组长度固定为 `Program::SHADER_TYPE_COUNT`（3） | 按上游；`PipelineLayoutKey` 的 `sizeof == 40` 断言保留 |
| 20 | `asyncPrewarmCache` 的 `PipelineDynamicOptions` | `stereoscopicType` 取了 `StereoscopicType::NONE` 的默认值 `StereoscopicType::None`；`.colorBlendOp = BlendEquation::SUBTRACT` 在本项目写作 `BlendEquation::Subtract` | 按本项目枚举值命名 |
| 21 | `VulkanBlitter` 用 `mCommands->getProtected()` 选择受保护命令缓冲 | 本项目 `VulkanCommands` **没有受保护池**（`getProtected()` / `CommandBufferPool` 的 `isProtected` 都未移植） | 统一用 `Get()`，已在代码注释与本表记录；受保护内容路径待变更 7 或后续变更补 |
| 22 | `VulkanReadPixels` 依赖 `DataReshaper` 完成像素整形 | `DataReshaper`（367 行 header-only）在变更 1 的 proposal 中被明确列为不移植；变更 4 亦已记录「不引入第二处数据整形实现」 | 在 `VulkanReadPixels.cpp` 内实现**限定范围**的行拷贝整形（组件类型与通道数一致时逐行拷贝，遵守目标 stride/alignment 与 left/top），需要跨类型/跨通道数转换时打 `LOG_WARN` 并原样拷贝——不静默产出错误布局。属已知局限，见第 9 节 |
| 23 | `VulkanProgram::writePushConstant` 的 stage 判定 `getVkStage` | 上游 `COMPUTE` 分支是 `PANIC_POSTCONDITION` | 改为返回 `VK_SHADER_STAGE_COMPUTE_BIT`（本项目只创建顶点/片元模块，该分支不可达），避免为一个不可达分支引入进程级 abort |
| 24 | `RasterState` 用 `VkBlendOp` / `VkCullModeFlags` 等 Vulkan 原生类型 | 一致；但本项目 `BlendEquation` 是 `enum class : uint8_t`，位域宽度 4 足够 | 字段名、类型、**顺序**逐字段对齐上游（`sizeof == 16` 断言保留），`CreatePipeline` 内改用 `static_cast<VkBlendOp>` |
| 25 | `PipelineKey` / `PipelineEqual` / `PipelineHashFn` 「提供」 | 上游三者都在 `private:` 段；`PipelineCacheEntry` 与 `GetOrCreatePipeline()` 亦为私有 | 按上游保持私有，**唯一例外**：`PipelineCacheEntry` 与 `GetOrCreatePipeline()` 提升为 public，并新增 `GetPipelineCount()`。理由：句柄值在 `vkDestroyPipeline` 后会被驱动复用（实测 `rebuiltHandle == firstHandle`），逐出与否只能以条目数观测，测试无法从私有段取证 |

### 8. `PipelineHashFn` 与容器替换的实测

- **哈希等价性**：本项目 `NS_UTILS::hash::Murmur3` 与上游 `utils::hash::murmur3` 实现逐行相同（同常量、同收尾、同雪崩），
  `MurmurHashFn` 同样以 seed 0、`sizeof(T)/4` 个字求哈希 → `PipelineKey` / `PipelineLayoutKey` / `LayoutKey` 的哈希值两边一致。
- **查找性能**（独立微基准，逐字段复刻 `PipelineKey`(320B) + `MurmurHashFn` + `memcmp` 判等，
  2,000,000 次命中查找，`-O2`，Apple M4，跑两轮取一致值）：

| 条目数 | `std::unordered_map` | `tsl::robin_map` |
|---|---|---|
| 16 | 114.6 / 115.5 ns | 100.1 / 100.4 ns |
| 256 | 100.7 / 100.9 ns | 103.7 / 103.8 ns |
| 4096 | 103.7 / 104.2 ns | 104.4 / 105.1 ns |

  结论：两者差距在 ±14% 内且随规模反转，4096 条目（远超实际管线数）时差 < 1%，属噪声量级——
  **替换管线缓存容器未观测到可测量的性能退化**；`robin_map` 的平坦存储优势被 320 字节的大键抵消。
  若后续发现退化，`PipelineMap` 是单个别名，回切成本极低。

### 9. 未验证面（局限）

- `VulkanBlitter::Blit` / `Resolve`：需要真实命令录制与渲染目标，`beginRenderPass` 接线（变更 7）前无调用方；
  本变更只验证构造与 `Terminate`，运行期行为以编译通过与逐行比对为准。
- `VulkanReadPixels::Run`：需要真实纹理 + 队列族下标 + 渲染目标；本变更只验证读回线程的投递/排空/join
  （`TaskHandler` 层面）与构造/终止不挂起。**注意**：上游的读回线程是 `Run` 首次调用时才创建的，
  故「构造后立即析构」本身并不覆盖 join——join 由 `TaskHandler` 用例直接覆盖。
- `VulkanDescriptorSetCache::Commit`：需要 `VulkanCommandBuffer`，属变更 7；本变更验证 `CreateSet` / `Bind` / `Unbind` / `Terminate`。
- 并行编译路径（`AsyncPrewarmCache` / `AddCachePrewarmCallback`）：需 `DriverConfig::vulkanEnableAsyncPipelineCachePrewarming`
  为真；默认关闭，本变更**未运行时验证**。并发入队（编译线程归零 + backend 线程 `Gc()`）同样未验证，
  以变更 1 的锁范围复核结论为准，**不宣称已并发验证**。
- `VulkanProgram` 的 `VkShaderModule` 泄漏：无 validation layer 时无法直接计数。现有证据是
  （a）析构路径被执行（池块归还断言）、（b）`vkDestroyShaderModule` 对两个非空句柄各调一次、（c）无 validation 报错。
- `VulkanAsyncHandles` 的 `PushConstantDescription::Write` 与 `FlushPushConstants`：需要真实命令录制，未运行期验证。
- `VulkanFence` / `VulkanSync`：创建路径属变更 7，未运行期验证（分类表的静态核对已完成）。
- `ResourceManager::Make<D, B>` 的池块尺寸取自**句柄实参的类型**，故调用方必须用具体类型
  `AllocHandle<VulkanDescriptorSet>()` 而非 `AllocHandle<HwDescriptorSet>()`——测试期实际踩到过
  （错用基类会写越界并在释放时命中 Arena 的 `FreeList::Push` 区间断言）。变更 7 的 `createDescriptorSetR`
  须遵循同一约定。
