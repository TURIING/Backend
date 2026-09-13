# Capability: vulkan-commands

## Purpose

命令录制层：`VulkanCommandBuffer` 录制、提交并跨帧借用 `Resource`；`VulkanCommandBufferPool` 以 45 槽位池 + 提交位图管理命令缓冲与内嵌围栏池；`VulkanCommands` 作为门面用信号量串起提交依赖链；`VulkanGroupMarkers` 在调试构建下维护跨缓冲的标记层级。受保护（protected）命令路径不在范围内；driver 接线由后续变更负责。

## Requirements

### Requirement: VulkanGroupMarkers 调试标记栈

`VulkanGroupMarkers` SHALL 以 `std::list<std::pair<NS_UTILS::String, Timestamp>>` 承载调试标记栈（`Timestamp = std::chrono::time_point<std::chrono::high_resolution_clock>`），提供 `Push(marker, start = {})`（`start` 为空时取 `now()`）、`Pop()`、`PopBottom()`、`Top()`、`Empty()`。整个类 SHALL 由 `BVK_ENABLED(BVK_DEBUG_GROUP_MARKERS)` 守卫，默认编译配置下不参与编译。

#### Scenario: 压栈与取栈顶

- **WHEN** 依次 `Push("a")`、`Push("b")` 后调用 `Top()`
- **THEN** 返回 `"b"` 及其时间戳；`Empty()` 为 false

#### Scenario: 两端弹出

- **WHEN** 对含两个标记的栈分别调用 `Pop()` 与 `PopBottom()`
- **THEN** `Pop()` 取最后压入者、`PopBottom()` 取最先压入者

#### Scenario: 默认配置不编译

- **WHEN** 以默认 `BVK_DEBUG_FLAGS` 编译
- **THEN** `VulkanGroupMarkers` 相关代码不参与编译，不影响构建

### Requirement: VulkanCommandBuffer 构造与句柄获取

`VulkanCommandBuffer` SHALL 提供构造 `(const VulkanContextPtr&, VulkanFencePool&, VkDevice, VkQueue, VkCommandPool, const VulkanSemaphoreManagerPtr&)` 并禁止拷贝：从命令池分配一个 `VK_COMMAND_BUFFER_LEVEL_PRIMARY` 命令缓冲（私有 helper，创建信息不设 flags，交由 `vkBeginCommandBuffer` 隐式重置）、经 `VulkanFencePool::AcquireFenceStatus()` 取得围栏状态、经 `VulkanSemaphoreManager::Acquire()` 取得提交信号量，并分配一个静态自增的 `Age()`。

SHALL 提供只读访问：`Buffer()`（`VkCommandBuffer`）、`Age()`、`GetVkFence()`、`GetFenceStatus()`（`shared_ptr<VulkanCmdFence>`）。类 SHALL 位于 `src/vulkan/commands/VulkanCommandBuffer.h/.cpp`。

#### Scenario: 构造取得三类句柄

- **WHEN** 构造 `VulkanCommandBuffer`
- **THEN** `Buffer()` 非空、`GetVkFence()` 为池分配的围栏、提交信号量已就绪且 `Age()` 大于前一个实例

#### Scenario: 年龄单调递增

- **WHEN** 连续构造两个 `VulkanCommandBuffer`
- **THEN** 后者的 `Age()` 严格大于前者

### Requirement: VulkanCommandBuffer 录制与调试标记

`VulkanCommandBuffer::Begin()` SHALL 以 `VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT` 调用 `vkBeginCommandBuffer`。`PushMarker`/`PopMarker`/`InsertEvent` SHALL 按上下文能力选择 `vkCmdBeginDebugUtilsLabelEXT` + `vkCmdEndDebugUtilsLabelEXT`/`vkCmdInsertDebugUtilsLabelEXT`（`IsDebugUtilsSupported()`）或 `vkCmdDebugMarkerBeginEXT` + `vkCmdDebugMarkerEndEXT`/`vkCmdDebugMarkerInsertEXT`（`IsDebugMarkersSupported()`），并维护 `m_markerCount`（`PopMarker` SHALL 断言计数大于 0）。

`Reset()` SHALL 清空标记计数、借用资源列表与等待信号量、重新分配 `Age`、重新取得提交信号量与围栏状态（旧围栏状态可能仍被其他方持有，故必须换新而非复用）。

#### Scenario: 未支持调试扩展时标记为空操作

- **WHEN** 上下文既不支持 debug utils 也不支持 debug markers 时调用 `PushMarker`
- **THEN** 仅 `m_markerCount` 递增，不发起任何 `vkCmd*` 调用

#### Scenario: 重置后状态归位

- **WHEN** 提交后调用 `Reset()`
- **THEN** 标记计数归零、借用资源被释放、`Age()` 增大，且提交信号量与围栏状态均为新对象

### Requirement: VulkanCommandBuffer 借用资源

`VulkanCommandBuffer` SHALL 提供模板 `Acquire(NS_UTILS::SharedPtr<T>)`（`T` 受限于 `Resource` 派生），把资源引用存入内部的 `std::vector<NS_UTILS::SharedPtr<Resource>>`；这些引用 SHALL 保持到 `Reset()` 或对象析构，从而使录制期间被借用的资源不会被 `ResourceManager::Gc()` 回收。

因 `Utils::SharedPtr` 不提供派生类到基类的隐式转换构造，`Acquire` SHALL 显式以 `SharedPtr<Resource>(ptr.Get())` 重建引用（`T = Resource` 时同样成立）。

#### Scenario: 借用期间引用计数保持

- **WHEN** 借用一个 `VulkanBuffer` 的 `SharedPtr` 后释放调用方的引用
- **THEN** 该对象引用计数仍大于 0，`ResourceManager::Gc()` 不会析构它

#### Scenario: 重置释放借用

- **WHEN** 调用 `Reset()` 且此前借用过资源
- **THEN** 借用列表被清空，对应对象引用计数减一（归零者进入 GC 队列）

### Requirement: VulkanCommandBuffer 提交

`Submit()` SHALL 返回 `NS_UTILS::SharedPtr<VulkanSemaphore>`：先弹出全部未闭合的调试标记，再 `vkEndCommandBuffer`；随后组装 `VkSubmitInfo`（等待信号量与其阶段掩码取自 `InsertWait` 累积的 `std::array` 与计数成员、`commandBufferCount = 1`、信号量数为 1 且指向本次提交信号量）；最后 `vkQueueSubmit`、`MarkSubmitted()`、计数归零并返回提交信号量。`VK_SUCCESS` 之外的返回值 SHALL 以 `LOG_ASSERT` 拦截。`Reset()` SHALL 同样把等待计数归零。

`InsertWait(VkSemaphore, VkPipelineStageFlags)` SHALL 把等待对写入两个 `std::array`（容量 `kMaxWaitSemaphores = 2`）的同一位置并推进共享计数，写入前 SHALL 以 `LOG_ASSERT(m_waitSemaphoreCount < kMaxWaitSemaphores)` 拦截越界。上游的 `fvkutils::StaticVector` SHALL NOT 移植。

#### Scenario: 提交成功返回信号量

- **WHEN** 录制后调用 `Submit()` 且 `vkQueueSubmit` 返回 `VK_SUCCESS`
- **THEN** 返回的信号量为本次提交信号量，围栏状态被置为 `VK_NOT_READY`，等待信号量列表被清空

#### Scenario: 未闭合标记被自动收尾

- **WHEN** 仍有标记未 `PopMarker` 时调用 `Submit()`
- **THEN** 全部标记先被弹出（含对应的 `vkCmd*End*` 调用）再结束命令缓冲

### Requirement: VulkanCommandBufferPool 构造与取录制缓冲

`VulkanCommandBufferPool`（上游名 `CommandBufferPool`）SHALL 提供构造 `(const VulkanContextPtr&, VkDevice, VkQueue, uint8_t queueFamilyIndex, const VulkanSemaphoreManagerPtr&)`：以 `VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT` 创建 `VkCommandPool`，并预创建 `kMaxCommandBuffers`（45）个 `VulkanCommandBuffer`；内嵌 `VulkanFencePool` 以该容量为 `minPoolSize`。

`ActiveBuffers` SHALL 为 `std::bitset<kMaxCommandBuffers>` 形态的提交位图。`GetRecording()` SHALL 在已有录制中缓冲时直接返回；否则扫描位图未置位者作为下一个录制槽，全部置位时循环 `Wait()` + `Gc()` 直至腾出槽位；取到槽位后调用其 `Begin()`。`IsRecording()` SHALL 以槽位索引是否为 `kInvalid` 判定。

析构 SHALL 依次 `Wait()`、`Gc()`、销毁命令池、`Terminate()` 围栏池。

#### Scenario: 池满时等待并回收

- **WHEN** 45 个槽位均已提交且未完成时调用 `GetRecording()`
- **THEN** 先 `Wait()` 等待全部围栏、`Gc()` 回收已完成者，再返回新腾出的录制缓冲

#### Scenario: 重复取用同一录制缓冲

- **WHEN** 在 `IsRecording()` 为真时再次调用 `GetRecording()`
- **THEN** 返回同一个 `VulkanCommandBuffer&`，不重新 `Begin()`

#### Scenario: 析构顺序

- **WHEN** 销毁 `VulkanCommandBufferPool`
- **THEN** 先等待并回收全部缓冲，再销毁 `VkCommandPool`，最后终止围栏池

### Requirement: VulkanCommandBufferPool 提交、回收与依赖

`Flush()` SHALL 在未录制时返回空引用，否则调用该缓冲的 `Submit()`、把槽位置位进提交位图、清空录制槽并返回提交信号量。`Gc()` SHALL 遍历已置位槽位，状态为 `VK_SUCCESS` 者经 `Reset()` 复位并从位图清除，随后调用围栏池的 `Gc()`。`Update()` SHALL 遍历已置位槽位调用其 `RefreshStatus(device)`。`Wait()` SHALL 收集全部已置位槽位的 `VkFence` 并 `vkWaitForFences(VK_TRUE, UINT64_MAX)`，随后 `Update()`。`WaitFor(VkSemaphore, VkPipelineStageFlags)` SHALL 在录制中时把该等待对转交给当前录制缓冲。

#### Scenario: 提交置位与复位

- **WHEN** 录制后 `Flush()`，围栏信号后再 `Gc()`
- **THEN** `Flush` 返回提交信号量且槽位置位；`Gc` 调用该缓冲的 `Reset()` 并清除其位

#### Scenario: 未录制时提交短路

- **WHEN** 未录制任何缓冲时调用 `Flush()`
- **THEN** 返回空 `SharedPtr<VulkanSemaphore>`，位图不变

#### Scenario: 等待全部在途提交

- **WHEN** 有 N 个已提交未完成的缓冲时调用 `Wait()`
- **THEN** 以 N 个围栏调用 `vkWaitForFences` 并调用 `Update()` 刷新状态

### Requirement: VulkanCommandBufferPool 组标记（调试）

在 `BVK_ENABLED(BVK_DEBUG_GROUP_MARKERS)` 下，`VulkanCommandBufferPool` SHALL 维护概念标记栈：`PushMarker(marker, timestamp)` 在栈为空时先创建，压栈后同时向当前录制缓冲发起 `PushMarker`；`PopMarker()` SHALL 断言栈非空，弹出后仅在录制中时向缓冲发起 `PopMarker`；`TopMarker()` SHALL 在栈空时返回空串；`InsertEvent(marker)` SHALL 转发给当前录制缓冲。

`GetRecording()` 在切换到新缓冲时 SHALL 把概念栈中残留的标记按自底向上的顺序重新压入新缓冲（保证跨缓冲的标记层级连续）。

#### Scenario: 栈空时取栈顶

- **WHEN** 从未压入标记时调用 `TopMarker()`
- **THEN** 返回空字符串

#### Scenario: 未录制时弹出只改概念栈

- **WHEN** 在未录制状态下调用 `PopMarker()`
- **THEN** 概念栈弹出该项，不发起任何 `vkCmd*End*` 调用

#### Scenario: 切换缓冲时重建标记

- **WHEN** 概念栈中残留标记时 `GetRecording()` 取得新缓冲
- **THEN** 这些标记按自底向上顺序重新压入新缓冲

### Requirement: VulkanCommands 门面

`VulkanCommands` SHALL 提供构造 `(VkDevice, VkQueue, uint32_t queueFamilyIndex, const VulkanContextPtr&, const VulkanSemaphoreManagerPtr&)`，构造时创建唯一一个 `VulkanCommandBufferPool`。SHALL 提供：

- `Get()`：返回该池的当前录制缓冲
- `Flush()`：池已销毁（`Terminate()` 之后）时返回 false；无录制中缓冲时返回 true 且不做任何提交
- `Wait()`/`Gc()`/`UpdateFences()`：池已销毁时直接返回，否则转发
- `AcquireFinishedSignal()`：取走并清空 `m_lastSubmit`
- `GetMostRecentFenceStatus()`：返回 `m_lastFenceStatus`（初值为 `VulkanCmdFence::Completed()`）
- `InjectDependency(VkSemaphore, VkPipelineStageFlags)`：记录一次性的外部依赖
- `Terminate()`：销毁池、清空最近提交信号量与最近围栏状态

#### Scenario: 无命令时 Flush 返回 false

- **WHEN** `Terminate()` 之后调用 `Flush()`
- **THEN** 返回 false，不访问任何池

#### Scenario: 最近围栏初值为已完成

- **WHEN** 构造后立即调用 `GetMostRecentFenceStatus()`
- **THEN** 返回状态为 `VK_SUCCESS` 的哨兵对象（无提交即视为全部完成）

### Requirement: VulkanCommands 提交的依赖链

`Flush()` SHALL 仅在池正在录制时执行提交：若存在注入依赖则先 `WaitFor` 注入信号量与其阶段；若存在 `m_lastSubmit` 则 `WaitFor` 该信号量并以 `VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT` 为等待阶段（仅依赖上一提交的片元输出与传输，放行顶点阶段以增加重叠）；随后取录制缓冲的围栏状态并 `Flush()` 之，再清空注入依赖、把 `m_lastSubmit` 更新为本次提交信号量、把 `m_lastFenceStatus` 更新为本次围栏状态。

#### Scenario: 无录制时不提交

- **WHEN** 池存在但没有录制中的命令缓冲时调用 `Flush()`
- **THEN** 返回 true，不发起提交，`m_lastSubmit` 与 `m_lastFenceStatus` 不变

#### Scenario: 注入依赖只生效一次

- **WHEN** `InjectDependency(sem, stage)` 后调用 `Flush()`
- **THEN** 该依赖被转交给当次录制缓冲，且 `m_injectedDependency` 被清空，下次 `Flush()` 不再使用

#### Scenario: 依赖链自持

- **WHEN** 连续两次 `Flush()` 且中间未取走提交信号量
- **THEN** 第二次以前一次的提交信号量作为等待依赖，形成有序执行链

### Requirement: VulkanPlatform 队列访问器

`VulkanPlatform` SHALL 新增 3 个只读访问器，把 `VulkanPlatformPrivate` 中已有但未暴露的图形队列信息开放出来：`GetGraphicsQueueFamilyIndex()`、`GetGraphicsQueueIndex()`、`GetVkGraphicsQueue()`。本次变更 SHALL NOT 引入调用方。

#### Scenario: 图形队列信息可取

- **WHEN** 平台初始化完成后调用 `GetVkGraphicsQueue()` 与 `GetGraphicsQueueFamilyIndex()`
- **THEN** 返回非空 `VkQueue` 与合法的队列族索引

### Requirement: 命令层适配约束

命令与 sync 各类型 SHALL 位于 `Backend` 命名空间（`BEGIN_NS_BACKEND`），公开 API PascalCase（`Get`/`Flush`/`Gc`/`Wait`/`Update`/`Terminate`/`Acquire`/`Submit`/`Reset`/`Begin`）——上游 `get`/`flush`/`gc`/`wait`/`update` 等小写形式 SHALL 改为 PascalCase，私有 helper 保持 camelCase。容器映射：`utils::bitset64` → `std::bitset<kMaxCommandBuffers>`、`utils::CString` → `NS_UTILS::String`；`fvkutils::StaticVector` SHALL NOT 移植（全仓仅一个使用点，以 `std::array<T, 2>` + 计数成员替代，见 design D12）；上游未被使用的 `BufferList`（`FixedCapacityVector`）typedef SHALL NOT 移植。断言/日志映射：`assert_invariant` → `LOG_ASSERT`、`FILAMENT_CHECK_POSTCONDITION` → 失败 `LOG_CRITICAL`、`FVK_LOGE` → `LOG_ERROR`、`FVK_SYSTRACE_*` SHALL NOT 移植。SHALL NOT 移植 `VulkanAsyncHandles.h` 中的 `VulkanProgram`/`VulkanFence`/`VulkanSync`/`VulkanTimerQuery` 与 `VulkanGroupMarkers` 之外的调试基础设施。

**文件组织**：`commands/` 下四个类 SHALL **一类一文件**——`VulkanGroupMarkers`/`VulkanCommandBuffer`/`VulkanCommandBufferPool`/`VulkanCommands` 各自拥有同名 `.h`/`.cpp`；`VulkanCommands.h` SHALL NOT 聚合其余三个类的定义。上游把四类同置 `VulkanCommands.h`，此处为有意的布局偏离（理由见 design D2）。

#### Scenario: 编译通过

- **WHEN** 全量构建 Backend target
- **THEN** `VulkanCommands`/`VulkanCommandBuffer`/`VulkanCommandBufferPool`/`VulkanGroupMarkers` 编译通过，不依赖 Filament 任何头文件与 `bluevk`

#### Scenario: 一类一文件

- **WHEN** 检查 `src/vulkan/commands/`
- **THEN** 存在四个同名 `.h`/`.cpp` 文件对；`VulkanCommands.h` 内不出现 `VulkanCommandBuffer`/`VulkanCommandBufferPool`/`VulkanGroupMarkers` 的类定义（只 include 其头文件）

#### Scenario: 命名与范围复核

- **WHEN** 复核新增文件
- **THEN** 公开 API 为 PascalCase；无 `FVK_SYSTRACE_*`；无 `VulkanProgram`/`VulkanSync`/`VulkanTimerQuery` 残留
