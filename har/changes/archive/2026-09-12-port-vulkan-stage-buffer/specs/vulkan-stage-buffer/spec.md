# Capability: vulkan-stage-buffer

## ADDED Requirements

### Requirement: 文件组织与命名

`VulkanStage` SHALL 改名为 `VulkanStageBuffer` 并与其嵌套类 `Segment` 一同位于 `src/vulkan/stage/VulkanStageBuffer.h/.cpp`；`VulkanStagePool` SHALL 位于 `src/vulkan/stage/VulkanStagePool.h/.cpp`。三者 SHALL 位于 `Backend` 命名空间（`BEGIN_NS_BACKEND`），公开 API 命名遵循项目规范（PascalCase），私有成员 `m_camelCase`，常量 `kPascalCase`。`VulkanStagePool.h` SHALL include `VulkanStageBuffer.h`；`VulkanStageBuffer.h` SHALL include `vulkan/resource/Resource.h`、`vulkan/resource/ResourceManager.h`、`vk_mem_alloc.h`。

#### Scenario: 目录与文件落位

- **WHEN** 检查 `src/vulkan/stage/`
- **THEN** 存在 `VulkanStageBuffer.h`/`VulkanStageBuffer.cpp`/`VulkanStagePool.h`/`VulkanStagePool.cpp` 四个文件，与 `buffer/`、`core/`、`platform/`、`resource/` 平级

#### Scenario: 类名改名生效

- **WHEN** 检索代码库中的 `VulkanStage`
- **THEN** 不存在独立的 `VulkanStage` 类定义，缓冲容器类名为 `VulkanStageBuffer`

### Requirement: VulkanStageBuffer 缓冲容器

`VulkanStageBuffer` SHALL public 继承 `NS_UTILS::Ref`，由 `VulkanStagePool` 以 `NS_UTILS::UniquePtr` **独占**持有（不共享给 `Segment`）；SHALL 持有 `VmaAllocator`、`VmaAllocation`、`VkBuffer`、`capacity`、宿主映射指针 `mapping`、当前切分偏移 `m_currentOffset` 与 `offset → Segment*` 映射表；SHALL 提供只读访问器 `GetMemory()`/`GetVkBuffer()`/`GetCapacity()`/`GetMapping()`/`GetCurrentOffset()`/`IsSafeToReset()`（无存活 Segment）与 `Reset()`（`m_currentOffset` 归零）。构造 SHALL 接受 `(VmaAllocator, VmaAllocation, VkBuffer, uint32_t capacity, void* mapping)`。

析构 SHALL 先 `LOG_ASSERT(IsSafeToReset())`（全部 Segment 已回收是销毁前置条件），再 `vmaDestroyBuffer` 释放 `VkBuffer` 与 `VmaAllocation`——即池 SHALL NOT 再自行承担 Vulkan 释放。拷贝与移动 SHALL 被禁止（由 `Ref` 保证，无需重复 `= delete` 声明）。

#### Scenario: 重置后可复用

- **WHEN** 缓冲的全部 Segment 释放后调用 `IsSafeToReset()` 再调用 `Reset()`
- **THEN** `IsSafeToReset()` 返回 true，`GetCurrentOffset()` 返回 0，缓冲可被池重新以满容量收录

#### Scenario: 禁止拷贝

- **WHEN** 尝试拷贝或移动 `VulkanStageBuffer`
- **THEN** 编译失败（由 `Ref` 已删除的拷贝/移动保证）

#### Scenario: 独占持有的释放

- **WHEN** 池释放某缓冲的 `UniquePtr`（`Terminate()` 或 `destroyStage` 路径）
- **THEN** 引用计数归零、`~VulkanStageBuffer` 执行 `vmaDestroyBuffer`，`VkBuffer` 与 `VmaAllocation` 被回收

#### Scenario: 析构前置于空

- **WHEN** 仍持有存活 Segment 时缓冲被销毁
- **THEN** 析构内的 `LOG_ASSERT(IsSafeToReset())` 失败并中止

### Requirement: VulkanStageBuffer::Segment 资源对象

`Segment` SHALL public 继承 `Resource`，持有父缓冲裸指针 `VulkanStageBuffer*`、`capacity`、`offset` 与 `OnRecycle = std::function<void(uint32_t offset)>` 回调；SHALL 仅经 `ResourceManager::AllocateAndConstruct<VulkanStageBuffer::Segment>(...)` 构造（不接受裸 `new`），使引用计数与延迟销毁走 `vulkan-resource` 通道。`Segment` SHALL 提供 `GetParentStage()`/`GetVkBuffer()`/`GetMemory()`/`GetCapacity()`/`GetOffset()`/`GetMapping()`（父缓冲 `mapping + offset`），其中 `GetVkBuffer()`/`GetMemory()` SHALL 转发父缓冲；SHALL 禁止拷贝与移动。

析构 SHALL 调用 `OnRecycleFn(offset)`，由父缓冲在其 `m_segments` 中摘除自身对应条目。

#### Scenario: 切分出的 Segment 可定位到父缓冲内存

- **WHEN** 取回偏移为 `o` 的 Segment，调用 `GetMapping()`
- **THEN** 返回值等于父缓冲 `GetMapping() + o`，`GetVkBuffer()`/`GetMemory()` 与父缓冲一致

#### Scenario: 析构摘除自身登记

- **WHEN** 最后一个 `SharedPtr<Segment>` 释放，GC 路径执行 `~Segment()`
- **THEN** `OnRecycleFn` 以该 Segment 的 `offset` 被调用，父缓冲 `m_segments` 中该偏移条目被移除；条目移除后 `IsSafeToReset()` 可返回 true

#### Scenario: 计数归零不立即析构

- **WHEN** `SharedPtr<Segment>` 引用归零
- **THEN** 不直接 `delete`，而是经 `Resource::OnLastRef` 入 GC 队列，待 `ResourceManager::Gc()` 统一析构

### Requirement: AcquireSegment 切分

`VulkanStageBuffer::AcquireSegment(ResourceManager&, uint32_t segmentOffset, uint32_t numBytes)` SHALL 返回 `NS_UTILS::SharedPtr<Segment>`：以 `(this, numBytes, segmentOffset, onRecycle)` 经 `AllocateAndConstruct` 构造，将 `segmentOffset → Segment*` 登记进 `m_segments`，并把 `m_currentOffset` 推进为 `segmentOffset + numBytes`。SHALL 约定 `numBytes` 已按物理设备 `nonCoherentAtomSize` 对齐（对齐由调用方 `VulkanStagePool` 负责），且 `segmentOffset` 不小于当前 `m_currentOffset`。

#### Scenario: 连续切分线性推进

- **WHEN** 对同一空缓冲依次 `AcquireSegment(rm, 0, 256)` 与 `AcquireSegment(rm, 256, 512)`
- **THEN** `GetCurrentOffset()` 依次为 256、768，`m_segments` 含两个条目

#### Scenario: 切分登记可回查

- **WHEN** 以偏移 `o` 切分后查询父缓冲的 `m_segments`
- **THEN** 存在键为 `o` 的条目且值为该 Segment

### Requirement: VulkanStagePool 构造与 AcquireStage

`VulkanStagePool` SHALL 提供构造 `(VulkanContext const&, ResourceManager&, VmaAllocator)` 并禁止拷贝；`AcquireStage(uint32_t numBytes, uint32_t alignment = 0)` SHALL 返回 `NS_UTILS::SharedPtr<VulkanStageBuffer::Segment>`，流程 SHALL 为：

1. `numBytes` 先经 `alignToNonCoherentAtomSize`（对齐到 `m_context.GetPhysicalDeviceLimits().nonCoherentAtomSize`）向上取整，保证后续 host flush 不波及相邻原子
2. 在以「剩余空间 → 缓冲」组织的 `std::multimap<uint32_t, NS_UTILS::UniquePtr<VulkanStageBuffer>>` 上 `lower_bound(numBytes)` 起线性扫描：以 `alignValue(currentOffset(), alignment)` 计算候选偏移，需同时满足不溢出（`segmentOffset >= currentOffset()`）与容量足够（`capacity() - numBytes >= segmentOffset`），命中则以 `std::move` 取出该独占引用并 `erase` 该节点
3. 未命中 SHALL 经 `allocateNewStage(max(numBytes, kStageSize))` 新建（返回独占引用，此时偏移为 0）
4. 经 `pStage->AcquireSegment(...)` 取得 Segment
5. 以 `capacity() - currentOffset()` 作为剩余空间把缓冲回插 map

`alignment` 为 0 SHALL 等价于不调整偏移；`alignValue` 遇 `alignment == 0` SHALL 原值返回，且 SHALL 不假定对齐值为 2 的幂（与 `CommandStream.h` 私有定义、仅支持 2 次幂的 `ALIGN_UP` 不同，故 SHALL 保留为 stage 模块内的文件级 helper）。

#### Scenario: 池内复用

- **WHEN** 先 `AcquireStage(n)` 并释放 Segment，再 `AcquireStage(m)` 且 `m <= n`、剩余空间足够
- **THEN** 复用同一 `VulkanStageBuffer`（不新建 VkBuffer），返回 Segment 的 `GetVkBuffer()` 与首次一致

#### Scenario: 池内不足时新建

- **WHEN** 请求大小超过池内全部缓冲的可用剩余空间
- **THEN** 经 `allocateNewStage` 新建容量为 `max(numBytes, kStageSize)` 的缓冲，新 Segment 偏移为 0

#### Scenario: 对齐产生空洞但不越界

- **WHEN** 以 `alignment > 1` 请求切分且 `currentOffset()` 非对齐
- **THEN** 候选偏移被上取整，仅在 `offset + numBytes <= capacity` 时复用该缓冲，否则继续扫描更大缓冲

#### Scenario: 新建缓冲以对齐后的字节数分配

- **WHEN** 请求 `numBytes` 小于 `kStageSize` 且未被对齐放大
- **THEN** 新缓冲容量为 `kStageSize`，VkBuffer 尺寸为 `alignToNonCoherentAtomSize(kStageSize)`

### Requirement: 暂存缓冲分配与销毁

`allocateNewStage(capacity)` SHALL 以 `VK_BUFFER_USAGE_TRANSFER_SRC_BIT` 创建 VkBuffer（`size = alignToNonCoherentAtomSize(capacity)`），分配信息 SHALL 采用本项目 VMA 3.x 写法：`.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT`、`.usage = VMA_MEMORY_USAGE_AUTO`，并 SHALL 直接取 `VmaAllocationInfo::pMappedData` 作为持久映射指针，不再调用 `vmaMapMemory`/`vmaUnmapMemory`；SHALL NOT 设置 `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT` 作为 requiredFlags。

返回值 SHALL 为 `NS_UTILS::UniquePtr<VulkanStageBuffer>`，以 `NS_UTILS::MakeUnique<VulkanStageBuffer>(allocator, memory, buffer, capacity, allocationInfo.pMappedData)` 构造（`UniquePtr` 不支持自定义删除器，故 Vulkan 释放只能内聚在对象析构里）。`destroyStage(stage)` SHALL 退化为释放该缓冲的独占引用（`UniquePtr::Reset()`），SHALL NOT 再调用 `vmaDestroyBuffer`。分配与回收 SHALL 在 `BVK_ENABLED(BVK_DEBUG_STAGING_ALLOCATION)` 下输出 `LOG_ERROR`（失败）/`LOG_DEBUG`（成功、销毁、回收）诊断。

#### Scenario: 分配成功且可写

- **WHEN** `AcquireStage(1)` 触发新建
- **THEN** VkBuffer 非空、映射指针非空（可 `memcpy` 写入）、usage 含 `VK_BUFFER_USAGE_TRANSFER_SRC_BIT`

#### Scenario: 分配失败可见

- **WHEN** `vmaCreateBuffer` 返回非 `VK_SUCCESS` 且启用 `BVK_DEBUG_STAGING_ALLOCATION`
- **THEN** 输出含 `VkResult` 的 `LOG_ERROR` 诊断

### Requirement: Gc 帧驱动淘汰与 Terminate

`Gc()` SHALL 先自增帧计数，并在 `<= kTimeBeforeEviction`（3）时提前返回：该帧计数为后续 image 淘汰路径保留（缓冲淘汰只判空、不依赖帧号），提前返回使淘汰节奏与上游一致；随后经 swap 遍历全部缓冲：`IsSafeToReset()` 为真者计数，超过 `kMaxEmptyStagesToRetain`（1）SHALL `destroyStage` 销毁，其余 SHALL `Reset()` 后以满容量 `capacity()` 回插；仍持有 Segment 者 SHALL 原样回插。`Terminate()` SHALL 销毁 map 中全部缓冲并清空 map，且 SHALL 在 `VkDevice` 仍存活时调用。

#### Scenario: 前 3 帧不淘汰

- **WHEN** `Gc()` 被调用 1~3 次
- **THEN** 帧计数自增后提前返回，不销毁任何缓冲

#### Scenario: 空缓冲按保留数淘汰

- **WHEN** 第 4 帧起存在多于 1 个空缓冲
- **THEN** 超出的空缓冲经 `destroyStage` 销毁，保留者 `Reset()` 后以满容量回插，仍在使用的缓冲原样回插

#### Scenario: 终止清空

- **WHEN** 全部 Segment 已回收后调用 `Terminate()`
- **THEN** map 中各 `UniquePtr` 释放、对应缓冲析构（`~VulkanStageBuffer` 内 `vmaDestroyBuffer`），map 为空

### Requirement: 线程与生命周期约束

`AcquireStage` SHALL 标注为**非线程安全**（仅驱动线程调用）。`VulkanStageBuffer::Segment` 的 `OnRecycle` 回调 SHALL 捕获其父 `VulkanStageBuffer` 的裸指针，且 `Segment` 自身持有父缓冲裸指针；因此 `VulkanStagePool` SHALL 比其产出的全部 Segment（含 GC 队列中待析构者）存活更久——调用方 SHALL 先 `ResourceManager::Terminate()` 排空 GC 队列，再调用 `VulkanStagePool::Terminate()`。

由于释放已内聚到 `~VulkanStageBuffer`，`VulkanStagePool` SHALL 先于 `VmaAllocator`/`VkDevice` 析构：池对象一经销毁，其 `m_stages` 成员必然析构并逐个释放 `UniquePtr`，从而触发全部缓冲的 Vulkan 释放——「忘记 `Terminate` 于是泄漏」不再是可选项，「在设备销毁之后释放」才是要避免的违约。

#### Scenario: 析构顺序错误可被发现

- **WHEN** 池在仍有 Segment 未析构时被销毁
- **THEN** 属调用方违约；设计约束要求在 `Terminate()` 前先排空 `ResourceManager` 的 GC 队列

#### Scenario: 跨帧持有

- **WHEN** 命令录制期间把 Segment 的 `SharedPtr` 存入命令缓冲以便跨帧持有
- **THEN** 引用计数保持该 Segment 存活，池的 `Gc()` 不会销毁其父缓冲（父缓冲 `IsSafeToReset()` 为假）

#### Scenario: 池先于设备销毁

- **WHEN** 池在 `VkDevice` 仍存活时被销毁（显式 `Terminate()` 或对象析构）
- **THEN** 全部缓冲经 `~VulkanStageBuffer` 释放，无泄漏；反之在 `VkDevice` 之后析构会触发 VMA/Vulkan 层错误

### Requirement: 适配约束

stage 各类型 SHALL 复用 `Backend` 命名空间的 `Resource`/`ResourceManager`/`Handle` 与 `NS_UTILS::SharedPtr`（`resource_ptr` 零改动替代）；`VulkanStageBuffer` 的自有内存 SHALL 经 `NS_UTILS::Ref` + `NS_UTILS::UniquePtr`/`MakeUnique` 管理——手工 `new`/`delete` 与池内 `vmaDestroyBuffer` 一并去掉，释放移入 `~VulkanStageBuffer`；断言/日志映射：`assert`/`assert_invariant` → `LOG_ASSERT`、`FVK_LOGD/FVK_LOGE` → `LOG_DEBUG`/`LOG_ERROR`、`FVK_ENABLED(FVK_DEBUG_STAGING_ALLOCATION)` → `BVK_ENABLED(BVK_DEBUG_STAGING_ALLOCATION)`；`FVK_SYSTRACE_CONTEXT/START/END` 与 `utils::io::endl` SHALL 不移植；`TIME_BEFORE_EVICTION`/`MAX_EMPTY_STAGES_TO_RETAIN`/`STAGE_SIZE` SHALL 落为文件级 `constexpr`（`kTimeBeforeEviction`/`kMaxEmptyStagesToRetain`/`kStageSize`，置于匿名命名空间）；头文件 SHALL 使用 `#pragma once`，include 顺序与注释 SHALL 遵循项目规范（不保留 Filament license/文件头注释，不写复述性注释）。`numBytes`/`capacity` 语义 SHALL 经 `using` 别名或参数名表达，不散落裸魔法数。

#### Scenario: 编译通过

- **WHEN** 全量构建 Backend target
- **THEN** `VulkanStageBuffer`/`VulkanStagePool` 编译通过，不依赖 Filament 任何头文件与 `resource_ptr`

#### Scenario: 调试日志开关

- **WHEN** 触发 `BVK_DEBUG_STAGING_ALLOCATION` 分支
- **THEN** 使用项目 `LOG_DEBUG`/`LOG_ERROR` 输出，无宏缺失与未使用变量告警
