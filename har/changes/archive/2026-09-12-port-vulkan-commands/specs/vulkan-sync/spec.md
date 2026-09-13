## ADDED Requirements

### Requirement: VulkanCmdFence 围栏状态包装

`VulkanCmdFence` SHALL 以 `std::shared_ptr` 共享持有，包装单个 `VkFence` 并携带状态机：初始 `VK_INCOMPLETE` 表示"尚未提交"，`MarkSubmitted()` SHALL 置 `VK_NOT_READY`，`RefreshStatus(VkDevice)` SHALL 经 `vkGetFenceStatus` 在返回 `VK_SUCCESS` 时置 `VK_SUCCESS`。状态读写 SHALL 由 `std::shared_mutex` 保护（`GetStatus()` 取共享锁），`SetStatus()` SHALL 置值并 `notify_all`。

SHALL 提供静态 `Completed()`：返回状态为 `VK_SUCCESS`、持有 `VK_NULL_HANDLE` 的实例（未提交任何命令即视为全部完成）。`GetVkFence()` SHALL 返回底层句柄。析构 SHALL 在回收回调非空时以 `mFence` 调用它。类 SHALL 位于 `src/vulkan/sync/VulkanCmdFence.h/.cpp`，由 `VulkanFencePool` 提供回收回调。

#### Scenario: 初始与提交状态

- **WHEN** 新建 `VulkanCmdFence` 后读取 `GetStatus()`，随后调用 `MarkSubmitted()`
- **THEN** 依次得到 `VK_INCOMPLETE` 与 `VK_NOT_READY`

#### Scenario: 围栏信号后刷新

- **WHEN** 对已提交的围栏调用 `RefreshStatus(device)` 且 `vkGetFenceStatus` 返回 `VK_SUCCESS`
- **THEN** `GetStatus()` 返回 `VK_SUCCESS`

#### Scenario: 已完成哨兵

- **WHEN** 调用 `VulkanCmdFence::Completed()`
- **THEN** 返回的对象 `GetStatus()` 为 `VK_SUCCESS`，`GetVkFence()` 为 `VK_NULL_HANDLE`

#### Scenario: 析构触发回收

- **WHEN** 最后一个 `shared_ptr<VulkanCmdFence>` 释放且构造时绑定了回收回调
- **THEN** 以该实例持有的 `VkFence` 调用回收回调，句柄归还其所属池

### Requirement: VulkanCmdFence 等待与取消

`VulkanCmdFence::wait(VkDevice, uint64_t timeout, std::chrono::steady_clock::time_point until)` SHALL 返回 `FenceStatus`：若状态仍为 `VK_INCOMPLETE`（未提交）SHALL 在条件变量上等待至 `until`，超时或被取消时分别返回 `FenceStatus::TimeoutExpired` / `FenceStatus::Error`；状态已为 `VK_SUCCESS` 或已被取消时 SHALL 立即返回对应结果；否则 SHALL 落 `vkWaitForFences`，按结果返回 `TimeoutExpired` / `ConditionSatisfied` / `Error`。

`Cancel()` SHALL 置取消标记并 `notify_all`，使正在等待的调用返回 `FenceStatus::Error`。等待期间 SHALL NOT 持锁调用 `vkWaitForFences`。

#### Scenario: 已提交围栏等待成功

- **WHEN** 对状态为 `VK_NOT_READY` 的围栏调用 `wait`，底层 `vkWaitForFences` 返回 `VK_SUCCESS`
- **THEN** 返回 `FenceStatus::ConditionSatisfied`，且状态被置为 `VK_SUCCESS`

#### Scenario: 未提交时被取消

- **WHEN** 围栏处于 `VK_INCOMPLETE`，调用 `Wait` 后另一线程调用 `Cancel()`
- **THEN** 等待被唤醒，返回 `FenceStatus::Error`

#### Scenario: 等待超时

- **WHEN** `vkWaitForFences` 返回 `VK_TIMEOUT`
- **THEN** 返回 `FenceStatus::TimeoutExpired`

### Requirement: VulkanFencePool 围栏池

`VulkanFencePool` SHALL 提供构造 `(const VulkanContextPtr&, VkDevice, uint32_t minPoolSize)`，构造时预分配 `minPoolSize` 个 `VkFence`（分配失败者不计入 `mNumFences`）。`AcquireFenceStatus()` SHALL 取一个空闲围栏并返回绑定回收回调（回收到本池）的 `shared_ptr<VulkanCmdFence>`，同时把它的 `weak_ptr` 登记进跟踪表。

`Gc()` SHALL 先清除跟踪表中已过期的 `weak_ptr`；`mCurrFrame` 自增后不超过 `kFenceTimeBeforeEviction`（3）时提前返回；之后在数量大于 `minPoolSize` 的前提下，淘汰 `frame + kFenceTimeBeforeEviction < mCurrFrame` 的空闲围栏。`Terminate()` SHALL 把跟踪表中仍存活者的回收回调**替换为直接 `vkDestroyFence`**（使它们不再归还到本池），再销毁池内全部空闲围栏并清空。`VkFenceCreateInfo` SHALL 在 `GetFenceExportFlags()` 非零时链上 `VkExportFenceCreateInfo`。类 SHALL 位于 `src/vulkan/sync/VulkanFencePool.h/.cpp`。

#### Scenario: 预分配与借出

- **WHEN** 以 `minPoolSize = N` 构造池并调用 `AcquireFenceStatus()`
- **THEN** 返回的围栏状态对象绑定了本池的回收回调，其 `weak_ptr` 已登记

#### Scenario: 归还即重置

- **WHEN** 借出的围栏状态对象析构触发回收
- **THEN** `vkResetFences` 被调用，该 `VkFence` 带着当前帧号回到空闲列表

#### Scenario: 前 3 帧不淘汰

- **WHEN** `Gc()` 被调用 1~3 次
- **THEN** 帧计数自增后提前返回，不销毁任何围栏

#### Scenario: 终止移交所有权

- **WHEN** 调用 `Terminate()` 且跟踪表中仍有存活的围栏状态对象
- **THEN** 这些对象的回收回调被替换为 `vkDestroyFence`，池内空闲围栏被销毁，再次回收不会写回已销毁的池

#### Scenario: 非零导出标志

- **WHEN** `VulkanContext::GetFenceExportFlags()` 非零且池分配新围栏
- **THEN** 创建信息链上 `VkExportFenceCreateInfo`，其 `handleTypes` 为该标志

### Requirement: VulkanSemaphore 资源对象

`VulkanSemaphore` SHALL public 继承 `Resource`，持有 `VulkanSemaphoreManager*` 与 `VkSemaphore`，经 `ResourceManager::AllocateAndConstruct<VulkanSemaphore>(manager, semaphore)` 构造（不接受裸 `new`）；SHALL 提供 `GetVkSemaphore()` 只读访问。析构 SHALL 调用 `mManager->Recycle(mSemaphore)` 把句柄归还信号量池。

#### Scenario: 析构归还信号量

- **WHEN** 最后一个 `NS_UTILS::SharedPtr<VulkanSemaphore>` 释放、GC 路径执行 `~VulkanSemaphore()`
- **THEN** 该 `VkSemaphore` 经 `Recycle` 回到 `VulkanSemaphoreManager` 的空闲池，未被 `vkDestroySemaphore`

#### Scenario: 只读访问

- **WHEN** 提交命令缓冲需要信号量句柄
- **THEN** 仅经 `GetVkSemaphore()` 获取，成员私有

### Requirement: VulkanSemaphoreManager 信号量池

`VulkanSemaphoreManager` SHALL 提供构造 `(VkDevice, const ResourceManagerPtr&)`，构造时预分配 `kMaxCommandBuffers` 个 `VkSemaphore`。`Acquire()` SHALL 优先取池尾的空闲句柄、池空则新建，并始终经 `ResourceManager::AllocateAndConstruct<VulkanSemaphore>(this, semaphore)` 返回 `NS_UTILS::SharedPtr<VulkanSemaphore>`。`Recycle(VkSemaphore)` SHALL 把句柄推回池。`Terminate()` SHALL `vkDestroySemaphore` 销毁池内全部句柄并清空。

#### Scenario: 池内取用与归还

- **WHEN** `Acquire()` 返回的信号量引用归零后再 `Acquire()`
- **THEN** 第二次取回同一 `VkSemaphore`（未额外创建）

#### Scenario: 池空时新建

- **WHEN** 空闲池已空时调用 `Acquire()`
- **THEN** 经 `vkCreateSemaphore` 新建句柄并包装为 `VulkanSemaphore`

#### Scenario: 终止销毁

- **WHEN** 调用 `Terminate()`
- **THEN** 池内全部 `VkSemaphore` 被 `vkDestroySemaphore` 销毁，池清空

### Requirement: Vulkan 共享常量

`src/vulkan/VkDef.h` SHALL 新增两个跨文件复用的常量：`kVkAlloc`（类型 `VkAllocationCallbacks const*`，值为 `nullptr`，用于标注 `vkCreate*`/`vkDestroy*` 的分配器实参）与 `kMaxCommandBuffers`（`3 * 15`，`VulkanCommands` 同时管理的命令缓冲上限）。仅单个文件使用的 `kFenceTimeBeforeEviction`（3）SHALL 留在 `VulkanFencePool.cpp` 的匿名命名空间，不上升为共享定义。

#### Scenario: 常量可跨文件复用

- **WHEN** `VulkanFencePool`/`VulkanSemaphoreManager`/`VulkanCommands` 引用分配器与容量上限
- **THEN** 分别使用 `kVkAlloc` 与 `kMaxCommandBuffers`，不重复书写字面量

#### Scenario: 文件私有常量不外泄

- **WHEN** 检查 `src/vulkan/VkDef.h`
- **THEN** 不出现 `kFenceTimeBeforeEviction`，该常量仅在 `VulkanFencePool.cpp` 内可见

### Requirement: FenceStatus 通用枚举

`include/Backend/DriverDefine.h` SHALL 新增 `enum class FenceStatus : int8_t`，取值语义与数值对齐上游 `backend/DriverEnums.h`：`Error = -1`、`ConditionSatisfied = 0`、`TimeoutExpired = 1`。枚举值命名 SHALL 遵循项目 C++ 规范（PascalCase）。上游同处的 `FENCE_WAIT_FOR_EVER` 本次 SHALL NOT 移植（无调用方）。

#### Scenario: 数值与上游一致

- **WHEN** 读取 `FenceStatus::Error` / `ConditionSatisfied` / `TimeoutExpired`
- **THEN** 底层值依次为 -1、0、1

#### Scenario: 供围栏等待返回

- **WHEN** `VulkanCmdFence::wait()` 走完各分支
- **THEN** 返回值类型为 `Backend::FenceStatus`，无需在 sync 模块内另定义枚举

### Requirement: 同步设施的线程与生命周期约束

`VulkanSemaphoreManager` SHALL 比其产出的全部 `VulkanSemaphore`（含 GC 队列中待析构者）存活更久——析构路径会回调 `Recycle`。`VulkanFencePool` SHALL 比引用它的全部 `VulkanCmdFence` 与 `VulkanCommandBuffer` 存活更久（后者持 `VulkanFencePool&`）。二者的 `Terminate()` SHALL 在 `VkDevice` 仍存活时调用。

#### Scenario: 顺序违约的后果被文档化

- **WHEN** 信号量池先于其产出的信号量析构
- **THEN** 属调用方违约，`Recycle` 将写入已析构对象；设计约束要求在 `Terminate()` 前先排空 `ResourceManager` 的 GC 队列

#### Scenario: 设备销毁前终止

- **WHEN** 在 `VkDevice` 仍存活时调用二者的 `Terminate()`
- **THEN** 池内 `VkFence`/`VkSemaphore` 被正常销毁；反之在设备之后调用会触发 Vulkan 层错误

### Requirement: 同步设施适配约束

sync 各类型 SHALL 位于 `Backend` 命名空间（`BEGIN_NS_BACKEND`），公开 API 命名 PascalCase、私有成员 `m_camelCase`、常量 `kPascalCase`；引用计数与持有 SHALL 经 `Utils::Ref` + `NS_UTILS::SharedPtr`（`resource_ptr` 零改动替代），`VulkanCmdFence` 保持 `std::shared_ptr` 形态（其共享语义不参与句柄池）。断言/日志映射：`assert_invariant` → `LOG_ASSERT`、`FILAMENT_CHECK_POSTCONDITION/PRECONDITION` → 失败 `LOG_CRITICAL`、`FVK_LOGE`/`LOG(ERROR)` → `LOG_ERROR`、`FVK_SYSTRACE_*` SHALL NOT 移植。头文件 SHALL 使用 `#pragma once`，include 顺序与注释 SHALL 遵循项目规范（不保留 Filament license/文件头注释，不写复述性注释）。

#### Scenario: 编译通过

- **WHEN** 包含 sync 各头文件并链接对应 `.cpp`
- **THEN** 编译通过，不依赖 Filament 任何头文件与 `resource_ptr`

#### Scenario: 命名与注释规范

- **WHEN** 检查新增文件
- **THEN** 公开 API 为 PascalCase、无 Filament license 头、无复述性注释
