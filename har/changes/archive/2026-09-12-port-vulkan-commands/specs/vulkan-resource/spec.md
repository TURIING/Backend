## MODIFIED Requirements

### Requirement: 类型表骨架

`ResourceType` 枚举 SHALL 完整移植 22 种 Filament 类型 + `UNDEFINED_TYPE`（必须为末位，用于枚举迭代）；`GetTypeEnum<D>()` 主模板 SHALL 返回 `UNDEFINED_TYPE`，已特化类型 SHALL 返回对应枚举：

- `VulkanBuffer` → `ResourceType::VulkanBuffer`（特化声明在 `Resource.h`、定义在 `Resource.cpp`，`Resource.h` 前向声明 `class VulkanBuffer`，不引入完整定义）
- `VulkanStageBuffer::Segment` → `ResourceType::StageSegment`（嵌套类型，特化声明在 `VulkanStageBuffer.h`、定义在 `VulkanStageBuffer.cpp`）
- `VulkanSemaphore` → `ResourceType::Semaphore`（特化声明在 `Resource.h`、定义在 `Resource.cpp`，`Resource.h` 前向声明 `struct VulkanSemaphore`，不引入完整定义）

`GetTypeStr(ResourceType)` SHALL 完整实现全部枚举值的字符串映射（不依赖类型定义）。枚举值命名与 Filament 一致（UPPER_SNAKE，与项目既有枚举现状一致）。

#### Scenario: 枚举完整性

- **WHEN** 遍历 `ResourceType` 0..UNDEFINED_TYPE-1
- **THEN** 22 种类型全部可枚举，`UNDEFINED_TYPE` 为末位哨兵

#### Scenario: VulkanBuffer 类型解析

- **WHEN** 调用 `Resource::GetTypeEnum<VulkanBuffer>()`
- **THEN** 返回 `ResourceType::VulkanBuffer`，不依赖 `VulkanBuffer` 完整定义即可编译

#### Scenario: StageSegment 类型解析

- **WHEN** 调用 `Resource::GetTypeEnum<VulkanStageBuffer::Segment>()`
- **THEN** 返回 `ResourceType::StageSegment`

#### Scenario: Semaphore 类型解析

- **WHEN** 调用 `Resource::GetTypeEnum<VulkanSemaphore>()`
- **THEN** 返回 `ResourceType::Semaphore`，不依赖 `VulkanSemaphore` 完整定义即可编译

#### Scenario: 已预埋的字符串映射

- **WHEN** 调用 `TransResourceTypeToStr(ResourceType::StageSegment)` / `(ResourceType::StageImage)` / `(ResourceType::Semaphore)`
- **THEN** 分别返回 `"StageSegment"` / `"StageImage"` / `"Semaphore"`（`port-vulkan-resource` 已预埋，本次不新增）

#### Scenario: 未特化类型回退

- **WHEN** 调用未特化类型（如 `VulkanTexture`）的 `GetTypeEnum`
- **THEN** 返回 `UNDEFINED_TYPE`（留待 VulkanHandles 移植）

#### Scenario: 未实例化不报错

- **WHEN** 仅使用 `GetTypeEnum` 主模板与 `GetTypeStr`
- **THEN** 编译通过，不引用任何 `Vulkan*` 类型定义

### Requirement: gc 与 terminate 批量销毁

`gc()` SHALL 将 GC 队列 swap 到局部列表（锁内），再逐个 `DestroyWithType(type, id)` 并清空；`terminate()` SHALL 循环 `gc()` 直至队列为空。`DestroyWithType` SHALL 处理以下分支：

- `VulkanBuffer`：经 `destruct<VulkanBuffer>(Handle<VulkanBuffer>(id))` 完成析构（`~VulkanBuffer` 触发 OnRecycle 回调归还缓存池）与 HandleAllocator 池块归还
- `StageSegment`：经 `destruct<VulkanStageBuffer::Segment>(Handle<VulkanStageBuffer::Segment>(id))` 完成析构（`~Segment` 触发 OnRecycle 回调，在父 `VulkanStageBuffer` 的 `m_segments` 中摘除自身条目）与 HandleAllocator 池块归还
- `Semaphore`：经 `destruct<VulkanSemaphore>(Handle<VulkanSemaphore>(id))` 完成析构（`~VulkanSemaphore` 触发 `VulkanSemaphoreManager::Recycle` 归还 `VkSemaphore`）与 HandleAllocator 池块归还

其余类型分支留待 VulkanHandles 移植补齐。`ResourceManager.cpp` SHALL include `vulkan/stage/VulkanStageBuffer.h` 与 `vulkan/sync/VulkanSemaphore.h` 以取得完整定义。

#### Scenario: 帧末回收 VulkanBuffer

- **WHEN** `VulkanBuffer` 引用归零入 GC 队列后调用 `gc()`
- **THEN** 该对象析构（OnRecycle 回调执行、gpuBuffer 归还缓存池），HandleAllocator 池块归还，计数 -1

#### Scenario: 帧末回收 StageSegment

- **WHEN** `VulkanStageBuffer::Segment` 引用归零入 GC 队列后调用 `gc()`
- **THEN** 该对象析构（父缓冲 `m_segments` 摘除对应条目），HandleAllocator 池块归还，计数 -1

#### Scenario: 帧末回收 Semaphore

- **WHEN** `VulkanSemaphore` 引用归零入 GC 队列后调用 `gc()`
- **THEN** 该对象析构（`VkSemaphore` 经 `Recycle` 回到信号量池），HandleAllocator 池块归还，计数 -1

#### Scenario: 退出回收

- **WHEN** 调用 `terminate()`
- **THEN** 重复 `gc()` 直到 GC 队列为空
