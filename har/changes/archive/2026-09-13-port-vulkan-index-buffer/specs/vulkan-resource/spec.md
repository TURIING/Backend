## MODIFIED Requirements

### Requirement: ResourceManager 句柄分配与构造

`ResourceManager` SHALL 包装 `HandleAllocatorVK`（`HandleAllocator<64, 160, 312>`），提供三个构造入口，并按"引用由谁持有"区分语义：

- `AllocHandle<D>()`：仅分配池块，返回 `Handle<D>`
- `Make<D, B>(handle, args...)`：在既有句柄上构造，返回**借用视图** `SharedPtr<D>`；构造后 SHALL 为句柄额外持有一份引用（对新建对象 `AddRef()`），使客户端句柄存活期间对象引用计数不为零、不被排入 GC 队列
- `AllocateAndConstruct<D>(args...)`：分配 + 构造，返回**拥有所有权**的 `SharedPtr<D>`（引用计数为 1，不额外持有句柄引用）；SHALL 直接经私有 `construct<D, D>` 实现，不经 `Make`

构造 SHALL 经 `HandleAllocator::Construct` 完成 placement-new 后调用 `Init<D>` 绑定元数据，并 SHALL 以 `obj->GetTypeEnum<D>()`（friend 访问私有成员，修正无对象调用）记录构造类型。构造函数 SHALL 接受 `arenaSize`、`disableUseAfterFreeCheck`、`disablePoolHandleTags` 三参数透传。私有 SHALL 提供 `destruct<D, B>(Handle<B>)`：`HandleCast` 解出对象指针 + `HandleAllocator::Deallocate` 归还池块，供 `DestroyWithType` 分支使用。

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

### Requirement: Destroy 入口与 double-destroy 检测

`ResourceManager` SHALL 提供 `Destroy<D>(SharedPtr<D>&)`，作为配 `Make` 使用的 driver 销毁入口：若对象销毁标记已置位 SHALL `LOG_CRITICAL` 报告重复销毁；否则依次置位销毁标记、`Reset()` 释放调用方借用引用、再 `SubRef()` 释放句柄持有的引用。释放句柄引用前 SHALL 以 `LOG_ASSERT(obj->GetRefCount() >= 1)` 校验借用视图确已释放（命令缓冲等第三方仍借用时计数可大于 1，此时 SHALL 由最后一方释放后归零）。

#### Scenario: 重复销毁

- **WHEN** 对同一对象连续两次调用 `Destroy`
- **THEN** 第二次触发 `LOG_CRITICAL` double-destroy 诊断并中止

#### Scenario: 单次销毁闭环

- **WHEN** 对 `Make` 创建的活跃对象调用 `Destroy`
- **THEN** 销毁标记置位、借用引用与句柄引用依次释放，计数归零后 `{type, id}` 入 GC 队列，对象内存保持不动直至 `gc()`

#### Scenario: 第三方仍借用时延后销毁

- **WHEN** 对象已被命令缓冲借用（计数 > 1）期间调用 `Destroy`
- **THEN** 释放两项引用后计数仍大于 0，对象 SHALL NOT 入 GC 队列，待借用方释放后归零入队

### Requirement: gc 与 terminate 批量销毁

`gc()` SHALL 将 GC 队列 swap 到局部列表（锁内），再逐个 `DestroyWithType(type, id)` 并清空；`terminate()` SHALL 循环 `gc()` 直至队列为空。`DestroyWithType` SHALL 处理以下分支：

- `VulkanBuffer`：经 `destruct<VulkanBuffer>(Handle<VulkanBuffer>(id))` 完成析构（`~VulkanBuffer` 触发 OnRecycle 回调归还缓存池）与 HandleAllocator 池块归还
- `VertexBufferInfo`：经 `destruct<VulkanVertexBufferInfo>(Handle<VulkanVertexBufferInfo>(id))` 完成析构与池块归还
- `BufferObject`：经 `destruct<VulkanBufferObject>(Handle<VulkanBufferObject>(id))` 完成析构与池块归还
- `IndexBuffer`：经 `destruct<VulkanIndexBuffer>(Handle<VulkanIndexBuffer>(id))` 完成析构与池块归还
- `VertexBuffer`：经 `destruct<VulkanVertexBuffer>(Handle<VulkanVertexBuffer>(id))` 完成析构与池块归还
- `StageSegment`：经 `destruct<VulkanStageBuffer::Segment>(Handle<VulkanStageBuffer::Segment>(id))` 完成析构（`~Segment` 触发 OnRecycle 回调，在父 `VulkanStageBuffer` 的 `m_segments` 中摘除自身条目）与 HandleAllocator 池块归还
- `Semaphore`：经 `destruct<VulkanSemaphore>(Handle<VulkanSemaphore>(id))` 完成析构（`~VulkanSemaphore` 触发 `VulkanSemaphoreManager::Recycle` 归还 `VkSemaphore`）与 HandleAllocator 池块归还

缓冲族四类（`VertexBufferInfo` / `BufferObject` / `IndexBuffer` / `VertexBuffer`）的完整定义由 `vulkan/VulkanHandle.h` 提供，`ResourceManager.cpp` SHALL include 该头文件；其余类型分支留待 VulkanHandles 其余类型移植补齐。

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
- **THEN** 重复 `gc()` 直到 GC 队列为空

### Requirement: 类型表骨架

`ResourceType` 枚举 SHALL 完整移植 22 种 Filament 类型 + `UNDEFINED_TYPE`（必须为末位，用于枚举迭代）；`GetTypeEnum<D>()` 主模板 SHALL 返回 `UNDEFINED_TYPE`，已特化类型 SHALL 返回对应枚举：

- `VulkanBuffer` → `ResourceType::VulkanBuffer`（特化声明在 `Resource.h`、定义在 `Resource.cpp`，`Resource.h` 前向声明 `class VulkanBuffer`，不引入完整定义）
- `VulkanBufferObject` → `ResourceType::BufferObject`（特化声明在 `Resource.h`、定义在 `Resource.cpp`，前向声明 `struct VulkanBufferObject`）
- `VulkanIndexBuffer` → `ResourceType::IndexBuffer`（特化声明在 `Resource.h`、定义在 `Resource.cpp`，前向声明 `struct VulkanIndexBuffer`）
- `VulkanVertexBufferInfo` → `ResourceType::VertexBufferInfo`（同上，前向声明 `struct VulkanVertexBufferInfo`）
- `VulkanVertexBuffer` → `ResourceType::VertexBuffer`（同上，前向声明 `struct VulkanVertexBuffer`）
- `VulkanStageBuffer::Segment` → `ResourceType::StageSegment`（嵌套类型，特化声明在 `VulkanStageBuffer.h`、定义在 `VulkanStageBuffer.cpp`）
- `VulkanSemaphore` → `ResourceType::Semaphore`（特化声明在 `Resource.h`、定义在 `Resource.cpp`，`Resource.h` 前向声明 `struct VulkanSemaphore`，不引入完整定义）

`TransResourceTypeToStr(ResourceType)` SHALL 完整实现全部枚举值的字符串映射（不依赖类型定义）。枚举值命名与 Filament 一致（UPPER_SNAKE，与项目既有枚举现状一致）。

#### Scenario: 枚举完整性

- **WHEN** 遍历 `ResourceType` 0..UNDEFINED_TYPE-1
- **THEN** 22 种类型全部可枚举，`UNDEFINED_TYPE` 为末位哨兵

#### Scenario: VulkanBuffer 类型解析

- **WHEN** 调用 `Resource::GetTypeEnum<VulkanBuffer>()`
- **THEN** 返回 `ResourceType::VulkanBuffer`，不依赖 `VulkanBuffer` 完整定义即可编译

#### Scenario: 缓冲族类型解析

- **WHEN** 调用 `Resource::GetTypeEnum<VulkanBufferObject>()` / `<VulkanIndexBuffer>()` / `<VulkanVertexBuffer>()` / `<VulkanVertexBufferInfo>()`
- **THEN** 分别返回 `ResourceType::BufferObject` / `IndexBuffer` / `VertexBuffer` / `VertexBufferInfo`，不依赖各类型完整定义即可编译

#### Scenario: StageSegment 类型解析

- **WHEN** 调用 `Resource::GetTypeEnum<VulkanStageBuffer::Segment>()`
- **THEN** 返回 `ResourceType::StageSegment`

#### Scenario: Semaphore 类型解析

- **WHEN** 调用 `Resource::GetTypeEnum<VulkanSemaphore>()`
- **THEN** 返回 `ResourceType::Semaphore`，不依赖 `VulkanSemaphore` 完整定义即可编译

#### Scenario: 已预埋的字符串映射

- **WHEN** 调用 `TransResourceTypeToStr(ResourceType::StageSegment)` / `(StageImage)` / `(Semaphore)`
- **THEN** 分别返回 `"StageSegment"` / `"StageImage"` / `"Semaphore"`

#### Scenario: 未特化类型回退

- **WHEN** 调用未特化类型（如 `VulkanTexture`）的 `GetTypeEnum`
- **THEN** 返回 `UNDEFINED_TYPE`（留待 VulkanHandles 其余类型移植）

#### Scenario: 未实例化不报错

- **WHEN** 仅使用 `GetTypeEnum` 主模板与 `TransResourceTypeToStr`
- **THEN** 编译通过，不引用任何 `Vulkan*` 类型定义
