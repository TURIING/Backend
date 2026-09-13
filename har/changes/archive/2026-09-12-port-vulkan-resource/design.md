# Design: port-vulkan-resource

## Context

Filament `backend/src/vulkan/memory/` 的资源层由三部分构成：`Resource`（intrusive 计数结构）、`ResourceManager`（句柄分配 + GC 延迟销毁）、`resource_ptr`（RAII 引用壳）。本项目已有移植的 `HandleAllocatorVK`（`HandleAllocator<64, 160, 312>`，池化分配 + age use-after-free 检测），以及双线程命令流架构（command-buffer-queue：记录线程 Flush / 执行线程 WaitForCommands）。VulkanDriver 目前是空壳（44 行 cpp），`VulkanHandles.h`（22 个 `Vulkan*` 对象类型）尚未移植。

本变更移植 `Resource` 与 `ResourceManager`（不移植 `resource_ptr`），以 `utils::Ref` + `utils::SharedPtr` 为引用计数与持有机制，为 VulkanDriver 提供资源生命周期层。

## Goals / Non-Goals

**Goals:**
- `Resource` 继承 `utils::Ref`，intrusive 原子计数，任意线程引用归零安全
- 保留延迟销毁（gc）：引用归零仅入 GC 队列，backend 线程 `gc()` 统一析构并归还 HandleAllocator 池块
- `Utils::SharedPtr` 零改动替代 `resource_ptr`
- use-after-free 语义级检测（Acquire/Destroy 入口）+ 既有 age 机制兜底
- 类型表最小骨架，编译立即通过

**Non-Goals:**
- 不移植 `VulkanHandles.h`（22 个 `Vulkan*` 类型）——`getTypeEnum` 特化与 `destroyWithType` 分支属后续独立变更
- 不移植 `resource_ptr` 及其 `inc()/dec()` 私有接口
- 不实现 VulkanDriver 的资源使用（driver 层集成是后续变更）
- 不调整 `HandleAllocator` 桶尺寸（`<64, 160, 312>` 现状；`Resource` 继承 Ref 后体积增大，若未来 `sizeof(D) > 312` 报 static_assert 再议）

## Decisions

### D1: 扩展 `Utils::Ref` 增加 `OnLastRef()` 虚回调（而非 Resource 隐藏 SubRef 或放弃 arena）

`Ref::SubRef()` 当前在计数归零时写死 `delete this`：对 HandleAllocator 池化分配（placement-new）的对象是 UB，且立即销毁违背延迟销毁语义。方案对比：

| 方案 | 做法 | 结论 |
|---|---|---|
| **A（选定）** | `SubRef` 归零时虚分发 `protected virtual OnLastRef()`，默认 `delete this` | 改动 3 行、完全向后兼容；Resource override 后归零只注册延迟销毁，对象内存由 gc 统一释放 |
| B | Resource 定义同签名 `SubRef()` 隐藏 Ref 版本 | 需访问 `m_refCount`（Ref private）不可行；且 `Ref*` 基类指针调用会绕过隐藏走 `delete this`，地雷 |
| C | Resource 放弃 arena 改堆分配 | 破坏 handle-allocator 能力域定位；且 vk 资源所有权需"掏出"交给 GC 列表，大改 |

### D2: `Utils::SharedPtr` 直接替代 `resource_ptr`

`resource_ptr` 的职责（RAII 增减计数、id 查询、类型转换）在 SharedPtr 架构下被拆分：计数由 `SharedPtr` + `Ref` 承担；id/类型查询由 `Resource::GetId()/GetResourceType()` 承担；handle→对象 转换由 `ResourceManager::Acquire` 承担。`SharedPtr` 依赖 `T : Ref` 的 static_assert 与 Resource 继承 Ref 正好匹配，零改动。

### D3: use-after-free 检测点重新分布

Filament 仅在 `resource_ptr::cast` 一处做语义级检测。SharedPtr 架构下检测点分散在生命周期各入口：

- **`Acquire<D, B>(handle) → SharedPtr<D>`**：handle→对象 唯一转换通道，检查销毁标记（used after freed）——替代 `resource_ptr::cast`
- **`Destroy<D>(SharedPtr<D>&)`**：driver 销毁入口，检查重复销毁（double-destroy）+ 置位销毁标记 + 释放引用
- **HandleAllocator age**（已有）：内存级兜底，抓"池块复用后的悬垂句柄"
- **gc/terminate 泄漏计数**：`BVK_DEBUG_RESOURCE_LEAK` 下构造/销毁计数平衡

约束：driver 层不得绕过 `Acquire` 直接 `HandleCast` 裸用（否则语义级检测失效）。

### D4: 砍掉 `ThreadSafeResource` 与 `isThreadSafeType` 分裂

Filament 分裂 `Resource`（24bit 非原子位域计数，backend 线程专用）与 `ThreadSafeResource`（atomic 计数）以省内存与原子开销。`utils::Ref` 计数本身是 `std::atomic<int32_t>`（fetch_add relaxed / fetch_sub acq_rel），统一继承后全类型天然线程安全，分裂失去意义，`ResourceManager::construct` 的 `if constexpr` 分支随之消除。

### D5: GC 列表合并为单列表 + `std::mutex`

Filament 因线程安全/非线程安全分裂 `mThreadSafeGcList`（锁）+ `mGcList`（无锁）。D4 后任意类型都可能跨线程归零入队，统一为单列表 + 锁。`gc()` 先 swap 到局部列表（锁内），锁外处理，避免持锁执行析构。

### D6: 类型表最小骨架

`ResourceType` 枚举 22+1 完整移植、`GetTypeStr` 完整（纯字符串映射，无类型依赖）；`GetTypeEnum<D>()` 仅主模板（返回 `UNDEFINED_TYPE`）；`destroyWithType` switch 仅 `UNDEFINED_TYPE` 空分支。原因：`getTypeEnum` 特化引用 `Vulkan*` 类型（`is_same` 只需前向声明），而 `destroyWithType` 的 `destruct<VulkanXxx>` 需完整类型定义（`~D()` + 归还池块）——类型定义属 VulkanHandles 移植范围。类型表以独立变更补齐，本次不阻塞编译。

### D7: `Resource` 成员与 getter

`Resource` 成员（`resManager`/`id`/`restype`/销毁标记）私有，`ResourceManager` 为 friend；driver 层经 `GetId()/GetResourceType()` 访问。Filament 中这些访问走 `resource_ptr`（friend + 内部字段），SharedPtr 架构下 driver 直接持有 `SharedPtr<D>`，getter 是唯一安全通道（符合项目"私有变量一律 getter"规则）。

### D8: 命名空间与适配映射

命名空间直接用 `Backend`（`BEGIN_NS_BACKEND`，不加 `fvkmemory` 子命名空间）。映射表：`assert_invariant`→`LOG_ASSERT`；`FILAMENT_CHECK_PRECONDITION << msg`→条件失败 `LOG_CRITICAL`（含诊断，abort）；`LOG(WARNING)`→`LOG_WARN`；`LOG(ERROR)`→`LOG_ERROR`；`FVK_ENABLED(FVK_DEBUG_RESOURCE_LEAK)`→`BVK_ENABLED(BVK_DEBUG_RESOURCE_LEAK)`（`src/vulkan/VkDef.h` 已有）；`utils::ImmutableCString`→`utils::ImmutableString`；`utils::Mutex`→`std::mutex`；`HandleBase::nullid`→`HandleBase::kNullId`。

## Risks / Trade-offs

- [Ref 改动影响面] → 改动仅 `SubRef` 尾部分发 + 新增虚函数，默认行为不变；编译全项目验证既有 Ref 使用者（Driver/VulkanContext 等）无回归
- [`OnLastRef` 虚函数使 Resource 增加 vptr] → `Resource` 体积约 24B（vptr + atomic + 成员），HandleAllocator 桶 `<64,160,312>` 余量足够；未来 `sizeof(D) > 312` 时 static_assert 会显式报出
- [driver 绕过 Acquire 裸用 HandleCast] → 语义级检测失效（仅剩 age 兜底）；以代码审查约束，spec 中已声明 Acquire 为唯一通道
- [gc 处理中递归入队] → 与 Filament 相同约束：派生对象析构（`~D()`）不应触发新的引用归零（D 不持有其他资源引用）；gc 用局部 swap 列表，新入队条目留待下一轮
- [泄漏计数仅 debug 构建生效] → 与 Filament 一致，release 零开销

## Open Questions

- `Destroy` 的调用形态：driver destroy API 持 `SharedPtr` 还是仅 `Handle`？若仅 Handle，需 `Destroy<D>(Handle<B>)`（内部 Acquire + 标记 + 释放）——待 VulkanDriver 移植时按实际使用确定，本次先提供 `Destroy(SharedPtr<D>&)`
- 若未来 VulkanHandles 移植需要 `Resource` 记录更多元数据（如 FVK_SYSTRACE 上下文），成员是否扩展——留待该变更决策
