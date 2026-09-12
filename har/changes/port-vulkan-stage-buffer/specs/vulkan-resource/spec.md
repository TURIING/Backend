# Capability: vulkan-resource

## ADDED Requirements

### Requirement: 嵌套类型资源的特化声明落位

`Resource::GetTypeEnum<D>()` 的显式特化 SHALL 声明于**能见到 `D` 完整定义的**头文件中。对嵌套类型（`VulkanStageBuffer::Segment`），外层类无法被前向声明成可用形式，故该特化 SHALL 声明于定义外层类的头文件（`vulkan/stage/VulkanStageBuffer.h`，类定义之后）、定义于同名 `.cpp`，SHALL NOT 强行放入 `vulkan/resource/Resource.h`。特化声明 SHALL 出现在任何 `AllocateAndConstruct<D>` 实例化点之前，否则主模板会静默返回 `UNDEFINED_TYPE`。

#### Scenario: 特化声明可见

- **WHEN** 编译 `VulkanStageBuffer.cpp`（`AcquireSegment` 内实例化 `AllocateAndConstruct<VulkanStageBuffer::Segment>`，特化声明已在同 TU 的头文件中先行可见）
- **THEN** `AllocateAndConstruct<VulkanStageBuffer::Segment>` 实例化时能看到特化声明，`GetTypeEnum` 返回 `STAGE_SEGMENT`

#### Scenario: Resource.h 不引入 stage 定义

- **WHEN** 检查 `vulkan/resource/Resource.h`
- **THEN** 不 include `vulkan/stage/VulkanStageBuffer.h`，不出现嵌套类型特化声明，无循环依赖

## MODIFIED Requirements

### Requirement: 类型表骨架

`ResourceType` 枚举 SHALL 完整移植 22 种 Filament 类型 + `UNDEFINED_TYPE`（必须为末位，用于枚举迭代）；`GetTypeEnum<D>()` 主模板 SHALL 返回 `UNDEFINED_TYPE`，已特化类型 SHALL 返回对应枚举：

- `VulkanBuffer` → `ResourceType::VulkanBuffer`（特化声明在 `Resource.h`、定义在 `Resource.cpp`，`Resource.h` 前向声明 `class VulkanBuffer`，不引入完整定义）
- `VulkanStageBuffer::Segment` → `ResourceType::StageSegment`（嵌套类型，特化声明在 `VulkanStageBuffer.h`、定义在 `VulkanStageBuffer.cpp`）

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

#### Scenario: 已预埋的字符串映射

- **WHEN** 调用 `TransResourceTypeToStr(ResourceType::StageSegment)` / `(ResourceType::StageImage)`
- **THEN** 分别返回 `"StageSegment"` / `"StageImage"`（`port-vulkan-resource` 已预埋，本次不新增）

#### Scenario: 未特化类型回退

- **WHEN** 调用未特化类型（如 `VulkanTexture`）的 `GetTypeEnum`
- **THEN** 返回 `UNDEFINED_TYPE`（留待 VulkanHandles 移植）

### Requirement: gc 与 terminate 批量销毁

`gc()` SHALL 将 GC 队列 swap 到局部列表（锁内），再逐个 `DestroyWithType(type, id)` 并清空；`terminate()` SHALL 循环 `gc()` 直至队列为空。`DestroyWithType` SHALL 处理以下分支：

- `VulkanBuffer`：经 `destruct<VulkanBuffer>(Handle<VulkanBuffer>(id))` 完成析构（`~VulkanBuffer` 触发 OnRecycle 回调归还缓存池）与 HandleAllocator 池块归还
- `StageSegment`：经 `destruct<VulkanStageBuffer::Segment>(Handle<VulkanStageBuffer::Segment>(id))` 完成析构（`~Segment` 触发 OnRecycle 回调，在父 `VulkanStageBuffer` 的 `m_segments` 中摘除自身条目）与 HandleAllocator 池块归还

其余类型分支留待 VulkanHandles 移植补齐。`ResourceManager.cpp` SHALL include `vulkan/stage/VulkanStageBuffer.h` 以取得嵌套类型完整定义。

#### Scenario: 帧末回收 VulkanBuffer

- **WHEN** `VulkanBuffer` 引用归零入 GC 队列后调用 `gc()`
- **THEN** 该对象析构（OnRecycle 回调执行、gpuBuffer 归还缓存池），HandleAllocator 池块归还，计数 -1

#### Scenario: 帧末回收 StageSegment

- **WHEN** `VulkanStageBuffer::Segment` 引用归零入 GC 队列后调用 `gc()`
- **THEN** 该对象析构（父缓冲 `m_segments` 摘除对应条目），HandleAllocator 池块归还，计数 -1

#### Scenario: 退出回收

- **WHEN** 调用 `terminate()`
- **THEN** 重复 `gc()` 直到 GC 队列为空
