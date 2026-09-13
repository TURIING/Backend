# Change: port-vulkan-commands

## Why

前三个变更建立的 `vulkan-resource` 生命周期层（引用归零 → GC 队列 → `DestroyWithType` → 归还池块）至今**没有真实消费者**：`VulkanBuffer`/`VulkanStageBuffer::Segment` 都是短暂持有即归还，跨帧保活这条路径从未被走到。命令录制层正是第一个消费者——`VulkanCommandBuffer` 在录制期间借用 Resource，必须保证提交完成前它们不被析构。

上游 `VulkanCommands.h` 把这个能力装在一个文件里，但它不是自足的：`VulkanCommandBuffer` 依赖 `VulkanFencePool`（提交围栏状态）与 `VulkanSemaphoreManager`（提交信号链），二者又分别依赖 `VulkanCmdFence` 与 `VulkanSemaphore`。因此本次把这条依赖闭包整体搬过来，作为后续 driver 帧循环接线的地基。

## What Changes

- 移植 `VulkanCommands.h/.cpp` 的**全部四个类**到 `src/vulkan/commands/`，且**一类一文件**：`VulkanGroupMarkers.h/.cpp`（调试标记栈）、`VulkanCommandBuffer.h/.cpp`、`VulkanCommandBufferPool.h/.cpp`（上游名 `CommandBufferPool`，改名）、`VulkanCommands.h/.cpp`（门面，单池）
- 一并移植同目录支撑件到 `src/vulkan/sync/`：`VulkanCmdFence`（**完整移植**，含 `wait()`/`cancel()`）、`VulkanFencePool`、`VulkanSemaphore`、`VulkanSemaphoreManager`
- 等待信号量数组**不移植**上游的 `fvkutils::StaticVector`：全仓仅 `VulkanCommandBuffer` 一处使用，改用 `std::array<T, 2>` + 一个共享计数成员（理由见 design D12）
- 常量落位：`kVkAlloc`（上游 `VKALLOC`）与 `kMaxCommandBuffers`（上游 `FVK_MAX_COMMAND_BUFFERS`，3×15）进 `src/vulkan/VkDef.h`；`kFenceTimeBeforeEviction` 保持 `VulkanFencePool.cpp` 文件级 `constexpr`（仅本文件使用）
- 新增通用定义 `FenceStatus`（`ERROR=-1`/`CONDITION_SATISFIED=0`/`TIMEOUT_EXPIRED=1`）到 `include/Backend/DriverDefine.h`——`VulkanCmdFence::wait()` 的返回类型，本项目此前没有
- 补充 `vulkan-resource` 类型表：`GetTypeEnum<VulkanSemaphore>` 特化、`DestroyWithType` 的 `Semaphore` 分支
- `VulkanPlatform` 补 3 个图形队列访问器（family index、queue index、`VkQueue`），把 `VulkanPlatformPrivate` 里已有但未暴露的值开放出来
- 适配映射：`utils::CString` → `NS_UTILS::String`；`utils::bitset64` → `std::bitset<kMaxCommandBuffers>`；`resource_ptr` → `NS_UTILS::SharedPtr`；`VulkanContext const&` → `const VulkanContextPtr&`
- **不移植受保护（protected）命令路径**：实测 macOS/MoltenVK 上 `protectedMemory = FALSE` 且无 `VK_QUEUE_PROTECTED_BIT` 队列族，该分支永不可达；命令层双池结构整体去掉，平台层的受保护内存能力保留（见 design D14）
- **不接线 `VulkanDriver`**：本次只落地类，`tick`/`flush`/`finish` 仍为空壳，沿用 `port-vulkan-buffer`/`port-vulkan-stage-buffer` 的口径

## Capabilities

### New Capabilities

- `vulkan-sync`: 提交同步原语——`VulkanCmdFence`（VkFence 状态包装与回收回调）、`VulkanFencePool`（VkFence 池化与淘汰）、`VulkanSemaphore` + `VulkanSemaphoreManager`（提交信号量的池化与 Resource 包装）与相关常量
- `vulkan-commands`: 命令录制层——`VulkanCommandBuffer`（单个命令缓冲的录制/提交/资源借用）、`VulkanCommandBufferPool`（45 槽位池 + 提交位图）、`VulkanCommands`（单池门面、依赖信号量注入、最近围栏查询）、`VulkanGroupMarkers`（调试标记栈）

### Modified Capabilities

- `vulkan-resource`: 类型表新增 `VulkanSemaphore` 特化与 `DestroyWithType` 的 `Semaphore` 分支（第三个真实类型资源）

## Impact

- 新增：`src/vulkan/sync/{VulkanCmdFence,VulkanFencePool,VulkanSemaphore,VulkanSemaphoreManager}.h/.cpp`、`src/vulkan/commands/{VulkanGroupMarkers,VulkanCommandBuffer,VulkanCommandBufferPool,VulkanCommands}.h/.cpp`（共 16 个文件）
- 修改：`src/vulkan/VkDef.h`（+2 常量）、`include/Backend/DriverDefine.h`（+`FenceStatus`）、`src/vulkan/resource/Resource.h/.cpp`（GetTypeEnum 特化）、`src/vulkan/resource/ResourceManager.cpp`（include + DestroyWithType 分支）、`include/Backend/platform/VulkanPlatform.h` + `src/vulkan/platform/VulkanPlatform.cpp`（+6 访问器）
- CMake 经 `file(GLOB_RECURSE)` 自动收集新增源文件，无需改动
- 不引入新依赖：`Utils/mem`、`Utils/string`、volk、VMA 均为既有依赖
- 运行期无调用方（预期内）：全部 16 个文件在本次变更后不被任何驱动路径调用，验证手段为全量构建 + 人工复核；`VulkanSemaphore` 的 Resource 销毁链与 `VulkanFencePool` 的回收路径的真实验证推迟到 driver 接线变更
- 平台队列访问器本次无调用方，属为接线变更预置
