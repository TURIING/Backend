# Capability: vulkan-query-blit-readback

## Purpose

Vulkan 的辅助渲染能力：计时查询池（`VulkanQueryManager`）、图像拷贝与 MSAA 解析（`VulkanBlitter`）、像素读回（`VulkanReadPixels`）。上游对应约 913 行。

三者都不在「画三角形」的最小路径上，但用户明确要求移植——它们是「143 方法全量对齐」的必要条件（`beginTimerQuery` / `endTimerQuery` / `getTimerQueryValue` / `blit` / `resolve` / `readPixels` / `readTexture` / `readBufferSubData` 共 8 条驱动方法的实现载体）。

## ADDED Requirements
### Requirement: VulkanQueryManager 计时查询池

`src/vulkan/VulkanQueryManager.{h,cpp}` SHALL 提供：

- `VulkanQueryManager(VkDevice, uint32_t queueFamilyIndex, VkPhysicalDeviceLimits const&)`：按 `timestampPeriod` / `timestampComputeAndGraphics` 判定计时支持
- `AcquireTimerQuery() → VulkanTimerQueryPtr`：从空闲池取，池空则创建新的 `VkQueryPool`
- `ReleaseTimerQuery(VulkanTimerQueryPtr)`：归还空闲池
- `BeginTimerQuery(VulkanCommandBuffer*, VulkanTimerQueryPtr)` / `EndTimerQuery(...)`：录制 `vkCmdWriteTimestamp`
- `GetTimerQueryValue(VulkanTimerQueryPtr, uint64_t* elapsedTime) → TimerQueryResult`：读回结果并换算为纳秒
- `Reset()` / `Terminate()`

`GetTimerQueryValue` SHALL 先检查 `VulkanTimerQuery::IsCompleted()`（依赖其持有的 `VulkanCmdFence` 状态），未完成时返回 `TimerQueryResult::NOT_READY` 而非阻塞或读无效值。

查询池 SHALL 在查询数达到 `VkQueryPool` 容量时新建池并切换。

#### Scenario: 未完成时返回 NOT_READY

- **WHEN** 查询尚未完成（fence 状态非 `VK_SUCCESS`）时调用 `GetTimerQueryValue`
- **THEN** 返回 `TimerQueryResult::NOT_READY`，不写 `elapsedTime`

#### Scenario: 完成后换算纳秒

- **WHEN** 查询完成且 `timestampPeriod` 为 P
- **THEN** `elapsedTime` 为 `(stop - start) * P`

#### Scenario: 池空时新建 QueryPool

- **WHEN** 连续获取超过单个 `VkQueryPool` 容量的查询
- **THEN** 新建 `VkQueryPool`，不越界写入

#### Scenario: Release 后可复用

- **WHEN** 释放一个 `VulkanTimerQuery` 后再次 `AcquireTimerQuery`
- **THEN** 复用该查询的下标

### Requirement: VulkanBlitter 拷贝与解析

`src/vulkan/VulkanBlitter.{h,cpp}` SHALL 提供：

- `Blit(VulkanCommandBuffer*, VulkanTexture const& src, VkImageBlit const&, VulkanTexture& dst, ...)`
- `Resolve(VulkanCommandBuffer*, ...)`：MSAA 解析（`vkCmdResolveImage`）
- 深度/模板格式的 blit 路径选择（`IsDepthStencilBlitSupported`）

依赖 `VulkanCommands`（录制命令）、`VulkanTexture`（源/目标）、`VulkanContext`（格式支持判定）。

#### Scenario: 颜色图像 blit

- **WHEN** 对两个颜色纹理调用 `Blit`
- **THEN** 录制 `vkCmdBlitImage`，源与目标布局转换正确

#### Scenario: MSAA 解析

- **WHEN** 以多采样源调用 `Resolve`
- **THEN** 录制 `vkCmdResolveImage`，目标为单采样图像

#### Scenario: 深度格式降级为拷贝

- **WHEN** 源格式为深度且设备不支持深度 blit
- **THEN** 走 `vkCmdCopyImage` 路径而非 `vkCmdBlitImage`

### Requirement: VulkanReadPixels 像素读回

`src/vulkan/VulkanReadPixels.{h,cpp}` SHALL 提供：

- 独立读回线程：`std::thread` + `std::condition_variable` + `std::queue`
- `ReadPixels(...)`：读回渲染目标区域
- `ReadTexture(...)`：读回纹理的某 level / layer
- `ReadBufferSubData(...)`：读回缓冲子区间
- `Terminate()`：停止并 join 读回线程

**生命周期约束**（SHALL 以注释写明）：读回线程 SHALL 在析构前 join。`VulkanDriver::DestroyResources()` 须在 `vmaDestroyAllocator` **之前**销毁 `VulkanReadPixels`——否则读回线程可能访问已释放的 allocator。

读回 SHALL 经暂存缓冲中转：`vkCmdCopyImageToBuffer` → `vmaMapMemory` → 拷贝到 `PixelBufferDescriptor` → `vmaUnmapMemory`。

依赖 `math::vec4`（变更 2 提供）。

#### Scenario: 构造与析构不挂起

- **WHEN** 构造 `VulkanReadPixels` 后立即析构
- **THEN** 读回线程被正确 join，析构不阻塞

#### Scenario: 读回请求经队列投递

- **WHEN** 调用 `ReadPixels(...)` 并传入非空的 `PixelBufferDescriptor`
- **THEN** 请求入队，读回线程处理后经 `PixelBufferDescriptor` 的释放回调通知调用方

#### Scenario: Terminate 后线程退出

- **WHEN** 调用 `Terminate()` 后再次调用
- **THEN** 幂等，不崩溃

#### Scenario: Terminate 释放暂存资源

- **WHEN** 调用 `Terminate()`
- **THEN** 读回用的暂存缓冲与命令资源释放，`vmaDestroyAllocator` 不触发泄漏断言

### Requirement: 适配约束
- 三个组件 SHALL 位于 `src/vulkan/`（`VulkanQueryManager.{h,cpp}` / `VulkanBlitter.{h,cpp}` / `VulkanReadPixels.{h,cpp}`）
- `tsl::robin_map` → `std::unordered_map`（若使用）；`utils::Mutex` → `std::mutex`；`utils::Condition` → `std::condition_variable`；`utils::Invocable` → `std::function`
- `math::vec4` 等类型 SHALL 由变更 2 的 `backend-math` 提供；若本能力域需要变更 2 未提供的 math 类型（如 `vec4` 的运算），SHALL 按需补充而非改用裸数组
- 线程生命周期约束 SHALL 以注释写明（`.dsh/rules/code-style.md` 允许对「隐含约束或前置条件」写注释）
- 上游文件头注释与 license 注释 SHALL 删除；`using namespace bluevk;` SHALL 删除
- `Resource.h` / `ResourceManager.cpp` SHALL 补 `TimerQuery` 的特化与销毁分支（若本变更内通过 `AllocateAndConstruct` 创建 `VulkanTimerQuery`，则需句柄背书的 `Make` 路径）
- `VulkanBlitter` 与 `VulkanReadPixels` SHALL NOT 持有 `VmaAllocator` 的裸拷贝后跨线程使用——SHALL 经 `VulkanStagePool` 获取暂存资源，由池负责 allocator 的生命周期

#### Scenario: 编译通过

- **WHEN** 构建整个 `Backend` 静态库
- **THEN** 本能力域的全部源文件编译通过，不依赖 Filament 任何头文件

#### Scenario: 命名与风格合规

- **WHEN** 检查本能力域新增的类型与函数
- **THEN** 命名遵循项目规范（`PascalCase` 类型与公有方法、`m_camelCase` 私有成员、`kPascalCase` 常量），头文件使用 `#pragma once`，不保留上游 license / 文件头注释
