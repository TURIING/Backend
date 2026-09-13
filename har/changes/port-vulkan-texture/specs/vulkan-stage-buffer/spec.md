## ADDED Requirements

### Requirement: VulkanStageImage 图像暂存

`src/vulkan/stage/VulkanStagePool.h` SHALL 新增 `class VulkanStageImage`（上游 `VulkanStagePool.h` 第 131-190 行）：

- 持有 `VkFormat` / `uint32_t width` / `uint32_t height` / `VmaAllocation` / `VkImage` / `uint64_t lastAccessed`
- 嵌套 `class Resource`：持 `VulkanStageImage*` + 回收回调（`using RecycleFn = std::function<void(VulkanStageImage*)>`），析构时经回调交还池
- 访问器：`Format()` / `Width()` / `Height()` / `Memory()` / `Image()` / `LastAccessed()`
- 禁止拷贝与移动

### Requirement: VulkanStagePool::AcquireStageImage

`VulkanStagePool` SHALL 新增 `AcquireStageImage(VkFormat format, uint32_t width, uint32_t height)`：按 `(format, width, height)` 三元组在池内查找可复用的 `VulkanStageImage`，命中则更新 `lastAccessed` 并返回；未命中则创建新的。

`Gc()` 的淘汰逻辑 SHALL 同时覆盖 `VulkanStageBuffer::Segment` 与 `VulkanStageImage`；`Terminate()` SHALL 释放两者持有的 VMA 内存。

#### Scenario: 同尺寸命中复用

- **WHEN** 以 `(VK_FORMAT_R8G8B8A8_UNORM, 256, 256)` 两次调用 `AcquireStageImage`（中间归还第一次的引用）
- **THEN** 第二次返回同一个 `VkImage` 句柄

#### Scenario: 不同尺寸不复用

- **WHEN** 以 `(format, 256, 256)` 与 `(format, 512, 512)` 分别获取
- **THEN** 返回不同的 `VkImage`

#### Scenario: 归还后可复用

- **WHEN** 释放 `VulkanStageImage::Resource` 引用后再次以相同参数获取
- **THEN** 复用同一 `VkImage`

#### Scenario: Terminate 释放图像内存

- **WHEN** 池内有存活的 `VulkanStageImage` 时调用 `Terminate()`
- **THEN** 全部 `VkImage` 与 `VmaAllocation` 释放，`vmaDestroyAllocator` 不触发泄漏断言

### Requirement: StageImage 类型表登记

`Resource.h` SHALL 补 `template <> ResourceType Resource::GetTypeEnum<VulkanStageImage>() noexcept;` 的特化声明（前向声明须用 `class VulkanStageImage;`——该类型是 `class` 而非 `struct`），定义放 `Resource.cpp`，返回 `ResourceType::StageImage`（枚举值 16，已在 `Resource.h` 预留）。

`ResourceManager::DestroyWithType` SHALL 补 `case ResourceType::StageImage:` 分支，经 `destruct<VulkanStageImage>(Handle<VulkanStageImage>(id))` 完成析构与池块归还。分支数由 7 增至 8。

#### Scenario: 类型解析

- **WHEN** 对 `VulkanStageImage` 调用 `GetTypeEnum<VulkanStageImage>()`
- **THEN** 返回 `ResourceType::StageImage`，非 `UndefinedType`

#### Scenario: 销毁归还池块

- **WHEN** `VulkanStageImage` 引用归零入 GC 队列后调用 `gc()`
- **THEN** 经 `StageImage` 分支析构（回收回调执行、`VkImage` 回池），HandleAllocator 池块归还

#### Scenario: 前向声明关键字正确

- **WHEN** 编译 `Resource.h` 的特化声明
- **THEN** `class VulkanStageImage;` 与定义处的 `class` 关键字一致，无「特化不匹配」错误

## MODIFIED Requirements

### Requirement: VulkanStagePool 构造与 AcquireStage

`VulkanStagePool` SHALL 提供两条获取路径：

- `AcquireStage(uint32_t numBytes, uint32_t alignment = 0) → VulkanStageBuffer::SegmentPtr`：缓冲暂存，行为与现状一致
- `AcquireStageImage(VkFormat format, uint32_t width, uint32_t height) → VulkanStageImage::ResourcePtr`：图像暂存（本次新增）

两条路径 SHALL 共享同一套 VMA 分配器与淘汰策略（按 `lastAccessed` 的 LRU 淘汰），但各自维护独立的空闲列表——缓冲与图像不可互换复用。

`VulkanStagePool` 的既有成员（`m_allocator` / `m_stageBuffers` / `m_freeSegments` 等）SHALL 保持现状不变；新增图像的对应成员与之成组放置。

#### Scenario: 两条路径互不干扰

- **WHEN** 交替调用 `AcquireStage` 与 `AcquireStageImage`
- **THEN** 各自返回正确类型的句柄，缓冲不会被当作图像复用，反之亦然

#### Scenario: 既有缓冲路径零回归

- **WHEN** 运行 `bin/BackendTests`（走 `AcquireStage` 路径）
- **THEN** 8 帧往返正常、exit 0，行为与本变更前完全一致
