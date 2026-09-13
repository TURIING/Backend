## ADDED Requirements

### Requirement: ThreadSafeResource 语义标记

`Resource.h` SHALL 新增 `struct ThreadSafeResource : public Resource`，作为**语义标记类型**：仅表示该类型的销毁须经线程安全队列入队，不引入第二套引用计数。

`ThreadSafeResource` SHALL NOT 自带计数器——`NS_UTILS::Ref` 已使用 `std::atomic<int32_t> m_refCount` 并提供 `OnLastRef()` 虚回调，线程安全的引用计数已经存在。`ThreadSafeResource` SHALL NOT 重写 `OnLastRef()`：归零动作仍由 `Resource::OnLastRef()` 统一处理（经 `resManager->DestructLaterWithType` 入队），入哪个队列由运行期的 `ResourceType` 决定。

该结构 SHALL 派生自 `Resource` 而非作为平行基类（偏离上游 `fvkmemory` 的平行结构，记录在案）。理由：本项目 `SharedPtr` 与 `HandleAllocator` 的既有约定是「所有资源型对象都是 `Resource`」，引入平行基类会使 `ResourceManager::Acquire` / `Destroy` / `HandleCast` 全部需要分支。

#### Scenario: 继承链正确

- **WHEN** 声明 `struct VulkanFence : public HwFence, public ThreadSafeResource`
- **THEN** `std::is_base_of_v<Resource, VulkanFence>` 为真，`Acquire<VulkanFence>` / `Destroy` 无需任何改动即可工作

#### Scenario: 不引入第二套计数

- **WHEN** 对 `ThreadSafeResource` 派生对象调用 `AddRef` / `SubRef` / `GetRefCount`
- **THEN** 走 `NS_UTILS::Ref` 的原子实现，行为与普通 `Resource` 完全一致

### Requirement: GC 队列分类表

`ResourceManager` SHALL 以 `isThreadSafeType(ResourceType)` 判定某类型应入哪个 GC 队列。该判定 SHALL 基于**运行期的 `ResourceType`**而非模板参数——`Resource::OnLastRef()` 只拿得到 `m_type`。

分类表 SHALL 以下列四条为线程安全（与上游 `fvkmemory` 的 `ThreadSafeResource` 派生类型一致）：

- `ResourceType::Program`（`VulkanProgram`）
- `ResourceType::Fence`（`VulkanFence`）
- `ResourceType::Sync`（`VulkanSync`）
- `ResourceType::TimerQuery`（`VulkanTimerQuery`）

其余类型 SHALL 为非线程安全。实现 SHALL 使用 `switch` + `CASE_FROM_TO`（项目宏规范禁止逐 case 手写两行），并以 `default: return false;` 兜底。

本变更只建立分类表，对应的四个资源类型实现属变更 6（`VulkanProgram` / `VulkanFence` 等）；分类表 SHALL 在本变更内先行就位，使变更 6 无需回头修改 `ResourceManager`。

#### Scenario: 分类判定

- **WHEN** 以 `ResourceType::Fence` 调用 `isThreadSafeType`
- **THEN** 返回 true；以 `ResourceType::Texture` 调用返回 false

#### Scenario: 未登记类型兜底

- **WHEN** 以 `ResourceType::UndefinedType` 调用
- **THEN** 返回 false，不触发断言

## MODIFIED Requirements

### Requirement: ResourceManager 句柄分配与构造

`ResourceManager` SHALL 包装 `HandleAllocatorVK`（`HandleAllocator<64, 160, 312>`），提供三个构造入口，并按"引用由谁持有"区分语义：

- `AllocHandle<D>()`：仅分配池块，返回 `Handle<D>`
- `Make<D, B>(handle, args...)`：在既有句柄上构造，返回**借用视图** `SharedPtr<D>`；构造后 SHALL 为句柄额外持有一份引用（对新建对象 `AddRef()`），使客户端句柄存活期间对象引用计数不为零、不被排入 GC 队列
- `AllocateAndConstruct<D>(args...)`：分配 + 构造，返回**拥有所有权**的 `SharedPtr<D>`（引用计数为 1，不额外持有句柄引用）；SHALL 直接经私有 `construct<D, D>` 实现，不经 `Make`

构造 SHALL 经 `HandleAllocator::Construct` 完成 placement-new 后调用 `Init<D>` 绑定元数据。**`Init<D>` 的调用目标 SHALL 按类型分派**：当 `D` 派生自 `ThreadSafeResource` 时 SHALL 以 `static_cast<ThreadSafeResource*>(obj)->Init<D>(...)` 调用，否则以 `static_cast<Resource*>(obj)->Init<D>(...)` 调用。该分派 SHALL 以 `if constexpr (std::is_base_of_v<ThreadSafeResource, D>)` 实现，编译期消解，不产生运行期分支。

构造 SHALL 以 `obj->GetTypeEnum<D>()`（friend 访问私有成员，修正无对象调用）记录构造类型。构造函数 SHALL 接受 `arenaSize`、`disableUseAfterFreeCheck`、`disablePoolHandleTags` 三参数透传。私有 SHALL 提供 `destruct<D, B>(Handle<B>)`：`HandleCast` 解出对象指针 + `HandleAllocator::Deallocate` 归还池块，供 `DestroyWithType` 分支使用。

#### Scenario: 分配并构造

- **WHEN** 调用 `AllocateAndConstruct<D>(args...)`
- **THEN** 返回持有有效 id 的 `SharedPtr<D>`，对象引用计数为 1，元数据已绑定，构造计数 +1

#### Scenario: 句柄背书对象的引用归句柄

- **WHEN** 调用 `Make<D, B>(handle, args...)` 并在语句结束时丢弃返回的 `SharedPtr`
- **THEN** 对象引用计数回到 1（句柄持有），对象 SHALL NOT 进入 GC 队列

#### Scenario: 实例化 construct 无编译错误

- **WHEN** 首次实例化 `construct<VulkanBuffer, ...>`（经 `AllocateAndConstruct<VulkanBuffer>`）
- **THEN** 编译通过，`GetTypeEnum<VulkanBuffer>` 特化被正确解析

#### Scenario: 销毁归还池块

- **WHEN** `DestroyWithType(VulkanBuffer, id)` 执行 `destruct<VulkanBuffer>`
- **THEN** 对象析构且 `Deallocate` 归还对应 HandleAllocator 池块

#### Scenario: 既有句柄上构造

- **WHEN** 以 `AllocHandle<D>()` 分配句柄后调用 `Make<D, D>(handle, args...)`
- **THEN** 返回指向该句柄对象的 `SharedPtr<D>`，id 不变

#### Scenario: 线程安全类型走同一构造路径

- **WHEN** 以 `Make<VulkanFence, HwFence>(handle)` 构造（`VulkanFence` 派生自 `ThreadSafeResource`）
- **THEN** `if constexpr` 分派到 `ThreadSafeResource::Init<D>`，元数据绑定成功，`Make` / `Acquire` / `Destroy` 的语义与普通 `Resource` 完全一致

### Requirement: 双 GC 列表与跨线程入队

`ResourceManager` SHALL 以**两个** `std::vector<std::pair<ResourceType, HandleId>>` 维护 GC 队列，各配独立 `std::mutex`：

- `m_gcList` + `m_gcListMutex`：非线程安全类型
- `m_threadSafeGcList` + `m_threadSafeGcListMutex`：线程安全类型（`isThreadSafeType` 判定）

`DestructLaterWithType` SHALL 按 `isThreadSafeType(type)` 分派到对应列表并持**对应**的锁 push。入队可发生于任意线程（Ref 计数原子，任意线程归零都可能入队）。

分裂两个列表的理由：线程安全类型的归零可能发生在编译线程或 app 线程（如并行编译完成时释放 `VulkanProgram`），与 backend 线程的 `Gc()` 并发；使用独立锁可使 backend 线程的 `Gc()` 在排空普通队列时不与这些线程争锁。

#### Scenario: 并发入队

- **WHEN** 多线程同时归零不同对象的引用
- **THEN** 全部 `{type, id}` 安全入队到各自列表，无数据竞争

#### Scenario: 按类型分派列表

- **WHEN** `VulkanProgram`（线程安全）与 `VulkanBuffer`（非线程安全）同时归零
- **THEN** 前者入 `m_threadSafeGcList`，后者入 `m_gcList`

#### Scenario: 两把锁互不阻塞

- **WHEN** 一线程持 `m_gcListMutex` 时另一线程归零线程安全类型对象
- **THEN** 后者可立即取得 `m_threadSafeGcListMutex` 完成入队，不被阻塞

### Requirement: gc 与 terminate 批量销毁

`gc()` SHALL 依次排空**两个** GC 队列：各自在锁内 swap 到局部列表，再逐个 `DestroyWithType(type, id)` 并清空。排空顺序 SHALL 为先线程安全队列、后普通队列（仅为约定，不构成正确性依赖）。

`terminate()` SHALL 循环 `gc()` 直至**两个**队列均为空。

`DestroyWithType` SHALL 处理以下分支：

- `VulkanBuffer`：经 `destruct<VulkanBuffer>(Handle<VulkanBuffer>(id))` 完成析构（`~VulkanBuffer` 触发 OnRecycle 回调归还缓存池）与 HandleAllocator 池块归还
- `VertexBufferInfo`：经 `destruct<VulkanVertexBufferInfo>(Handle<VulkanVertexBufferInfo>(id))` 完成析构与池块归还
- `BufferObject`：经 `destruct<VulkanBufferObject>(Handle<VulkanBufferObject>(id))` 完成析构与池块归还
- `IndexBuffer`：经 `destruct<VulkanIndexBuffer>(Handle<VulkanIndexBuffer>(id))` 完成析构与池块归还
- `VertexBuffer`：经 `destruct<VulkanVertexBuffer>(Handle<VulkanVertexBuffer>(id))` 完成析构与池块归还
- `StageSegment`：经 `destruct<VulkanStageBuffer::Segment>(Handle<VulkanStageBuffer::Segment>(id))` 完成析构（`~Segment` 触发 OnRecycle 回调，在父 `VulkanStageBuffer` 的 `m_segments` 中摘除自身条目）与 HandleAllocator 池块归还
- `Semaphore`：经 `destruct<VulkanSemaphore>(Handle<VulkanSemaphore>(id))` 完成析构（`~VulkanSemaphore` 触发 `VulkanSemaphoreManager::Recycle` 归还 `VkSemaphore`）与 HandleAllocator 池块归还

缓冲族四类（`VertexBufferInfo` / `BufferObject` / `IndexBuffer` / `VertexBuffer`）的完整定义由 `vulkan/VulkanHandle.h` 提供，`ResourceManager.cpp` SHALL include 该头文件；`Program` / `Fence` / `Sync` / `TimerQuery` 四类分支留待变更 6 补齐（本变更只建立分类表，不引入对应类型）。

#### Scenario: 帧末回收 VulkanBuffer

- **WHEN** `VulkanBuffer` 引用归零入 GC 队列后调用 `gc()`
- **THEN** 该对象析构（OnRecycle 回调执行、gpuBuffer 归还缓存池），HandleAllocator 池块归还，计数 -1

#### Scenario: 帧末回收缓冲族对象

- **WHEN** `VulkanIndexBuffer` / `VulkanBufferObject` / `VulkanVertexBuffer` 引用归零入 GC 队列后调用 `gc()`
- **THEN** 各自经对应分支析构（`VulkanIndexBuffer` 触发其 `VulkanBufferProxy` 析构、底层 `VulkanBuffer` 归还缓存池），HandleAllocator 池块归还，计数 -1

#### Scenario: 帧末回收 StageSegment

- **WHEN** `VulkanStageBuffer::Segment` 引用归零入 GC 队列后调用 `gc()`
- **THEN** 该对象析构（父缓冲 `m_segments` 摘除对应条目），HandleAllocator 池块归还，计数 -1

#### Scenario: 帧末回收 Semaphore

- **WHEN** `VulkanSemaphore` 引用归零入 GC 队列后调用 `gc()`
- **THEN** 该对象析构（`VkSemaphore` 经 `Recycle` 回到信号量池），HandleAllocator 池块归还，计数 -1

#### Scenario: 退出回收

- **WHEN** 调用 `terminate()`
- **THEN** 重复 `gc()` 直到**两个** GC 队列均为空

#### Scenario: 线程安全队列非空时 terminate 不提前返回

- **WHEN** `m_gcList` 为空但 `m_threadSafeGcList` 非空时调用 `terminate()`
- **THEN** 继续 `gc()`，直到线程安全队列也被排空

## REMOVED Requirements

### Requirement: 单 GC 列表与跨线程入队

**Reason**: 该需求明确要求「以单个 `std::vector` + `std::mutex` 维护 GC 队列（不分裂线程安全/非线程安全两列表）」。`VulkanProgram` / `VulkanFence` / `VulkanSync` / `VulkanTimerQuery` 四类（上游均继承 `fvkmemory::ThreadSafeResource`）的归零可能发生在编译线程或 app 线程，与 backend 线程的 `gc()` 并发；单列表使 backend 线程的 `gc()` 必须与这些线程争同一把锁。

**Migration**: 由「双 GC 列表与跨线程入队」需求取代。`DestructLaterWithType` 的公开签名不变，调用方（`Resource::OnLastRef`）零改动；`gc()` / `terminate()` 的公开签名不变，调用方（`VulkanDriver::tick` / `terminate`）零改动。
