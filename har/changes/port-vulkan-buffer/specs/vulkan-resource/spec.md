# Capability: vulkan-resource

## MODIFIED Requirements

### Requirement: 类型表骨架

`ResourceType` 枚举 SHALL 完整移植 22 种 Filament 类型 + `UNDEFINED_TYPE`（必须为末位，用于枚举迭代）；`GetTypeEnum<D>()` 主模板 SHALL 返回 `UNDEFINED_TYPE`，`VulkanBuffer` 特化 SHALL 返回 `ResourceType::VulkanBuffer`（特化声明在 `Resource.h`、定义在 `Resource.cpp`，`Resource.h` 前向声明 `class VulkanBuffer`，不引入完整定义）；`GetTypeStr(ResourceType)` SHALL 完整实现全部枚举值的字符串映射（不依赖类型定义）。枚举值命名与 Filament 一致（UPPER_SNAKE，与项目既有枚举现状一致）。

#### Scenario: 枚举完整性

- **WHEN** 遍历 `ResourceType` 0..UNDEFINED_TYPE-1
- **THEN** 22 种类型全部可枚举，`UNDEFINED_TYPE` 为末位哨兵

#### Scenario: VulkanBuffer 类型解析

- **WHEN** 调用 `Resource::GetTypeEnum<VulkanBuffer>()`
- **THEN** 返回 `ResourceType::VulkanBuffer`，不依赖 `VulkanBuffer` 完整定义即可编译

#### Scenario: 未特化类型回退

- **WHEN** 调用未特化类型（如 `VulkanTexture`）的 `GetTypeEnum`
- **THEN** 返回 `UNDEFINED_TYPE`（留待 VulkanHandles 移植）

### Requirement: gc 与 terminate 批量销毁

`gc()` SHALL 将 GC 队列 swap 到局部列表（锁内），再逐个 `DestroyWithType(type, id)` 并清空；`terminate()` SHALL 循环 `gc()` 直至队列为空。`DestroyWithType` SHALL 处理 `VulkanBuffer` 分支：经 `destruct<VulkanBuffer>(Handle<VulkanBuffer>(id))` 完成析构（`~VulkanBuffer` 触发 OnRecycle 回调归还缓存池）与 HandleAllocator 池块归还；其余类型分支留待 VulkanHandles 移植补齐。

#### Scenario: 帧末回收 VulkanBuffer

- **WHEN** `VulkanBuffer` 引用归零入 GC 队列后调用 `gc()`
- **THEN** 该对象析构（OnRecycle 回调执行、gpuBuffer 归还缓存池），HandleAllocator 池块归还，计数 -1

#### Scenario: 退出回收

- **WHEN** 调用 `terminate()`
- **THEN** 重复 `gc()` 直到 GC 队列为空

### Requirement: ResourceManager 句柄分配与构造

`ResourceManager` SHALL 包装 `HandleAllocatorVK`（`HandleAllocator<64, 160, 312>`），提供 `AllocHandle<D>()`（仅分配，返回 `Handle<D>`）、`Make<D, B>(handle, args...)`（在既有句柄上构造，返回 `SharedPtr<D>`）、`AllocateAndConstruct<D>(args...)`（分配+构造，返回 `SharedPtr<D>`）。构造 SHALL 经 `HandleAllocator::Construct` 完成 placement-new 后调用 `Init<D>` 绑定元数据，并 SHALL 以 `obj->GetTypeEnum<D>()`（friend 访问私有成员，修正无对象调用）记录构造类型。构造函数 SHALL 接受 `arenaSize`、`disableUseAfterFreeCheck`、`disablePoolHandleTags` 三参数透传。私有 SHALL 提供 `destruct<D, B>(Handle<B>)`：`HandleCast` 解出对象指针 + `HandleAllocator::Deallocate` 归还池块，供 `DestroyWithType` 分支使用。

#### Scenario: 分配并构造

- **WHEN** 调用 `AllocateAndConstruct<D>(args...)`
- **THEN** 返回持有有效 id 的 `SharedPtr<D>`，对象位于 HandleAllocator 池内且元数据已绑定，构造计数 +1

#### Scenario: 实例化 construct 无编译错误

- **WHEN** 首次实例化 `construct<VulkanBuffer, ...>`（经 `AllocateAndConstruct<VulkanBuffer>`）
- **THEN** 编译通过，`GetTypeEnum<VulkanBuffer>` 特化被正确解析

#### Scenario: 销毁归还池块

- **WHEN** `DestroyWithType(VulkanBuffer, id)` 执行 `destruct<VulkanBuffer>`
- **THEN** 对象析构且 `Deallocate` 归还对应 HandleAllocator 池块
