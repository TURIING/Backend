# Tasks: port-vulkan-stage-buffer

## 1. VulkanStageBuffer 移植

- [x] 1.1 创建 `src/vulkan/stage/VulkanStageBuffer.h`：`VulkanStageBuffer` 类（`#pragma once`，include `Backend/DriverDefine.h`/`vk_mem_alloc.h`/`vulkan/resource/Resource.h`/`vulkan/resource/ResourceManager.h`/`Utils/mem/SharedPtr.h`）；构造 `(VmaAllocation, VkBuffer, uint32_t, void*)`；访问器 `GetMemory()`/`GetVkBuffer()`/`GetCapacity()`/`GetMapping()`/`GetCurrentOffset()`/`IsSafeToReset()` 与 `Reset()`；`AcquireSegment(ResourceManager&, uint32_t segmentOffset, uint32_t numBytes)` 声明；成员 `m_memory`/`m_vkbuffer`/`m_capacity`/`m_mapping`/`m_currentOffset`/`m_segments`；拷贝与移动 `= delete`
- [x] 1.2 同头文件定义嵌套 `Segment : public Resource`：`using OnRecycle = std::function<void(uint32_t offset)>`；构造 `(VulkanStageBuffer*, uint32_t capacity, uint32_t offset, OnRecycle&&)`；`~Segment()` 调用 `m_onRecycleFn(m_offset)`；访问器 `GetParentStage()`/`GetVkBuffer()`/`GetMemory()`（转发父缓冲）/`GetCapacity()`/`GetOffset()`/`GetMapping()`（父 `mapping + offset`）；拷贝与移动 `= delete`
- [x] 1.3 在 `VulkanStageBuffer.h` **类定义之后**声明特化 `template <> ResourceType Resource::GetTypeEnum<VulkanStageBuffer::Segment>() noexcept;`（嵌套类型无法前向声明，不得放入 `Resource.h`）
- [x] 1.4 创建 `src/vulkan/stage/VulkanStageBuffer.cpp`：实现 `AcquireSegment`——经 `resourceManager.AllocateAndConstruct<Segment>(this, numBytes, segmentOffset, onRecycle)` 构造（`onRecycle` 为 `[this](uint32_t offset) { m_segments.erase(offset); }`），登记 `m_segments.insert({ segmentOffset, segment.Get() })`，推进 `m_currentOffset = segmentOffset + numBytes`，返回 `SharedPtr<Segment>`
- [x] 1.5 在 `VulkanStageBuffer.cpp` 定义特化返回 `ResourceType::StageSegment`

## 2. 资源层类型表接入

- [x] 2.1 `src/vulkan/resource/ResourceManager.cpp` include `vulkan/stage/VulkanStageBuffer.h`（取得嵌套类型完整定义；与既有 `vulkan/buffer/VulkanBuffer.h` 的 include 聚拢放置）
- [x] 2.2 同文件 `destroyWithType` 增加分支：`case ResourceType::StageSegment: destruct<VulkanStageBuffer::Segment>(Handle<VulkanStageBuffer::Segment>(id)); break;`
- [x] 2.3 复核 `vulkan/resource/Resource.h` **不改动**：不 include stage 头、不出现嵌套类型特化声明、无循环依赖；确认已预埋的 `ResourceType::StageSegment` 枚举与 `TransResourceTypeToStr` 映射无需新增

## 3. VulkanStagePool 移植

- [x] 3.1 创建 `src/vulkan/stage/VulkanStagePool.h`：include `vulkan/stage/VulkanStageBuffer.h`/`vulkan/VulkanContext.h`/`vulkan/resource/ResourceManager.h`；构造 `(VulkanContext const&, ResourceManager&, VmaAllocator)`；`AcquireStage(uint32_t numBytes, uint32_t alignment = 0)` 返回 `NS_UTILS::SharedPtr<VulkanStageBuffer::Segment>`；`Gc()`/`Terminate()`；私有 `alignToNonCoherentAtomSize(uint32_t)`/`allocateNewStage(uint32_t)`/`destroyStage(VulkanStageBuffer const*)`；成员 `m_context`/`m_resourceManager`/`m_allocator`/`std::multimap<uint32_t, VulkanStageBuffer*> m_stages`/`m_currentFrame`；拷贝 `= delete`
- [x] 3.2 创建 `src/vulkan/stage/VulkanStagePool.cpp`：匿名命名空间定义 `constexpr uint32_t kStageSize = 1 << 20`、`kMaxEmptyStagesToRetain = 1`、`kTimeBeforeEviction = 3` 与文件级 helper `alignValue(uint32_t value, uint32_t alignment)`（`alignment == 0` 原值返回，不假定 2 的幂，**不**替换为 `CommandStream.h` 的 `ALIGN_UP`）
- [x] 3.3 实现 `AcquireStage`（照抄上游算法）：先 `alignToNonCoherentAtomSize(numBytes)`；`m_stages.lower_bound(numBytes)` 起线性扫描，候选偏移取 `alignValue(currentOffset(), alignment)`，需同时满足 `segmentOffset >= currentOffset()` 与 `capacity() - numBytes >= segmentOffset`，命中则 `erase` 该缓冲；未命中则 `allocateNewStage(std::max(numBytes, kStageSize))`（偏移 0）；随后 `AcquireSegment`；最后以 `capacity() - currentOffset()` 为剩余空间回插 `m_stages`
- [x] 3.4 实现 `alignToNonCoherentAtomSize`：取 `m_context.GetPhysicalDeviceLimits().nonCoherentAtomSize` 后交 `alignValue`
- [x] 3.5 实现 `allocateNewStage`：`VkBufferCreateInfo{ .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .size = alignToNonCoherentAtomSize(capacity) }` + `VmaAllocationCreateInfo{ .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, .usage = VMA_MEMORY_USAGE_AUTO }`（**不**设 `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`），取 `VmaAllocationInfo::pMappedData` 作持久映射，`new VulkanStageBuffer(...)`，`BVK_ENABLED(BVK_DEBUG_STAGING_ALLOCATION)` 下输出 `LOG_ERROR`/`LOG_DEBUG` 诊断（不移植 `FVK_SYSTRACE_*`）
- [x] 3.6 实现 `destroyStage`：`LOG_ASSERT(stage->IsSafeToReset())` → `vmaDestroyBuffer` → `delete`（**不再** `vmaUnmapMemory`），日志同上
- [x] 3.7 实现 `Gc()`：`++m_currentFrame <= kTimeBeforeEviction` 提前返回；swap 出 map 后遍历——`IsSafeToReset()` 者计数，超过 `kMaxEmptyStagesToRetain` 则 `destroyStage`，否则 `Reset()` 后以 `capacity()` 回插；仍在用者原样回插
- [x] 3.8 实现 `Terminate()`：销毁 map 中全部缓冲（经 `destroyStage`）并清空

## 4. VulkanStageBuffer 所有权改造（A 形态，见 design D10）

> 任务组 3 记录的是改造前落地的形态；其中 `m_stages` 的裸指针值类型（3.1）、`new VulkanStageBuffer`（3.5）、`destroyStage` 内的 `vmaDestroyBuffer` + `delete`（3.6）已由本组任务取代，不作为现状依据。

- [x] 4.1 `VulkanStageBuffer.h`：改为 `class VulkanStageBuffer : public NS_UTILS::Ref`（`Ref` 经 `Resource.h` 已可见）；新增成员 `VmaAllocator m_allocator`；构造签名改为 `(VmaAllocator allocator, VmaAllocation memory, VkBuffer vkbuffer, uint32_t capacity, void* mapping)`；删去 4 行冗余的拷贝/移动 `= delete`（`Ref` 已删除拷贝与移动）
- [x] 4.2 `VulkanStageBuffer.h/.cpp`：把 `LOG_ASSERT(IsSafeToReset())` 与 `vmaDestroyBuffer(m_allocator, m_vkbuffer, m_memory)` 迁入 `~VulkanStageBuffer`（承担全部 Vulkan 释放），并在 `BVK_ENABLED(BVK_DEBUG_STAGING_ALLOCATION)` 下保留销毁日志
- [x] 4.3 `VulkanStagePool.h`：include `Utils/mem/UniquePtr.h`；`m_stages` 值类型改 `NS_UTILS::UniquePtr<VulkanStageBuffer>`；`allocateNewStage` 返回类型改 `NS_UTILS::UniquePtr<VulkanStageBuffer>`；`destroyStage` 签名改为接收 `NS_UTILS::UniquePtr<VulkanStageBuffer>&`（或直接删去该 helper）
- [x] 4.4 `VulkanStagePool.cpp`：`allocateNewStage` 改 `NS_UTILS::MakeUnique<VulkanStageBuffer>(m_allocator, memory, buffer, capacity, allocationInfo.pMappedData)`；`AcquireStage` 命中项改 `std::move` 取出该独占引用、回插改 `m_stages.insert({ ..., std::move(stage) })`；`Gc()` 的 swap 遍历改移动语义（`insert(std::move(pair))`，不可拷贝）；`destroyStage`/`Terminate` 退化为 `Reset()` 掉 `UniquePtr`
- [x] 4.5 复核：`grep -n "vmaDestroyBuffer\|new VulkanStageBuffer\|delete stage" src/vulkan/stage/VulkanStagePool.cpp` 应无命中（释放已内聚到对象）；`Segment` 仍持裸父指针（未引入共享语义）；用一次性最小程序验证「池 `Terminate()`」与「池对象析构」两条路径都会触发 `~VulkanStageBuffer` 的释放

## 5. 构建验证与复核

- [x] 5.1 全量构建（`cmake --build build`）：通过——`Backend` 与 `BackendTests` 均构建成功，本次新增/改动文件无警告（仅剩 `3rd/Utils` 既有的 `nonportable-include-path`）。原先的阻塞点 `VulkanDriver::Create` 形参不匹配已修复（改为 `VulkanPlatformPtr(platform)` / `VulkanContextPtr(&context)`）；`DriverAPI.inc` 新增的 `CreateVertexBuffer` 也补齐了缺失的 `CreateVertexBufferR` 定义
- [ ] 5.2 `tests` 运行期验证（确认既有功能无回归）：**构建通过，运行期被既有缺口阻塞**——`DriverConfig::handleArenaSize` 默认 0 且全仓库无人设置，`VulkanDriver` 将其透传给 `ResourceManager` → `HandleAllocatorVK` 构造零尺寸 Arena → `FreeList::Init` 断言失败；另需 `DYLD_LIBRARY_PATH` 指向 Homebrew 才能 dlopen 到 Vulkan loader
- [x] 5.3 复核范围边界：`grep -rn "StageImage\|AcquireImage\|PixelDataFormat\|VulkanLayout\|VulkanCommands\|FVK_" src/vulkan/stage` 无残留；`alignValue` 未被替换为 `ALIGN_UP`；`Resource.h`/`ResourceManager.h` 无 stage 相关改动
- [x] 5.4 复核命名与规范：公开 API PascalCase、私有成员 `m_camelCase`、常量 `kPascalCase`、无 Filament license/文件头注释、无复述性注释；clang-format 非 include 部分差异清零（include 顺序按项目既有约定保留）——已随任务组 4 重跑
