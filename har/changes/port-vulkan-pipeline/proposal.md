# Change: port-vulkan-pipeline

## Why

这是「画出三角形」的核心：把管线状态、着色器模块、描述符集转换为 `VkPipeline`，并提供绘制所需的资源对象。上游对应组件合计约 3200 行，是全部变更中最大的一块。

组件清单与依赖：

| 组件 | 行数 | 在本变更中的角色 |
|---|---|---|
| `VulkanAsyncHandles.{h,cpp}` | 574 | `VulkanProgram` / `PushConstantDescription` / `VulkanFence` / `VulkanSync` / `VulkanTimerQuery` |
| `VulkanPipelineCache.{h,cpp}` | 825 | 管线状态机 + `VkPipeline` 创建与缓存 |
| `VulkanPipelineLayoutCache.{h,cpp}` | 172 | `VkPipelineLayout` 缓存 |
| `VulkanDescriptorSetLayoutCache.{h,cpp}` | 247 | `VkDescriptorSetLayout` 缓存 |
| `VulkanDescriptorSetCache.{h,cpp}` | 584 | 描述符集的绑定状态与提交 |
| `VulkanQueryManager.{h,cpp}` | 169 | 计时查询池 |
| `VulkanBlitter.{h,cpp}` | 240 | MSAA 解析与 blit |
| `VulkanReadPixels.{h,cpp}` | 504 | 像素读回（独立读回线程） |
| `VulkanRenderPrimitive` + `VulkanProgram` 的资源对象部分 | 约 250 | 追加到既有 `VulkanHandle.h` |

**本变更首次让 `ThreadSafeResource` 有实际使用者**——`VulkanProgram` / `VulkanFence` / `VulkanSync` / `VulkanTimerQuery` 四个类型全部继承它（变更 1 已把分类表就位，本变更是其运行时验证点）。

## What Changes

### 1. `VulkanAsyncHandles`（574 行）

- `PushConstantDescription`：push constant 范围构建与写入（依赖 `backend::Program`）
- `VulkanProgram`：`VkShaderModule` 持有者 + push constant 队列 + 并行编译取消标记
- `VulkanFence`：`HwFence` 资源对象，持 `std::shared_ptr<VulkanCmdFence>`
- `VulkanSync`：`HwSync` 资源对象，持 `Platform::Sync*` + `CallbackData` 列表
- `VulkanTimerQuery`：`HwTimerQuery` 资源对象，持起止查询下标 + `std::shared_ptr<VulkanCmdFence>`
- **`VulkanCmdFence` 不移植**——本项目已在 `src/vulkan/sync/VulkanCmdFence.h` 有完整实现（探索阶段逐行比对确认语义一致）

**ThreadSafeResource 适配**：四类型均改为 `public ThreadSafeResource`（变更 1 新增），`utils::Mutex` → `std::mutex`，`std::atomic<bool>` 保留。

### 2. `VulkanPipelineCache`（825 行）

- 管线状态机：`BindProgram` / `BindRasterState` / `BindStencilState` / `BindRenderPass` / `BindPrimitiveTopology` / `BindVertexArray` / `BindLayout` / `BindPipeline`
- `GetOrCreatePipeline()` → `CreatePipeline()`：组装 `VkGraphicsPipelineCreateInfo`
- `PipelineKey` / `PipelineEqual` / `PipelineHashFn`：管线缓存键
- `Gc()` / `Terminate()` / `ResetBoundPipeline()`
- **并行编译路径的处置**（见 design D1）：`AsyncPrewarmCache` / `AddCachePrewarmCallback` 保留，`CompilerThreadPool` + `CallbackManager`（变更 1 已就位）
- `tsl::robin_map` → `std::unordered_map`（**高风险替换点**，design D2）

### 3. `VulkanPipelineLayoutCache`（172 行）

- `DescriptorSetLayoutArray`（`VkDescriptorSetLayout` 数组）
- `GetLayout(vkLayouts, program)` → `VkPipelineLayout`
- 含 push constant 范围的合并逻辑

### 4. `VulkanDescriptorSetLayoutCache`（247 行）

- `CreateLayout(handle, DescriptorSetLayout)` → `VulkanDescriptorSetLayoutPtr`
- `GetVkLayout(bitmask, externalSamplerBitmask, externalSamplers)`：按位掩码组合缓存

### 5. `VulkanDescriptorSetCache`（584 行）

- 绑定状态：`Bind(setIndex, set, offsets)` / `Unbind(setIndex)` / `GetBoundSets()`
- `Commit(VulkanCommandBuffer*, VkPipelineLayout, DescriptorSetMask)`：实际调用 `vkCmdBindDescriptorSets`
- 描述符集的 `VkDescriptorSet` 分配与写入

### 6. `VulkanQueryManager`（169 行）

- 计时查询池：`AcquireTimerQuery()` / `ReleaseTimerQuery()`
- `BeginTimerQuery` / `EndTimerQuery` / `GetTimerQueryValue`
- 依赖 `VulkanAsyncHandles` 的 `VulkanTimerQuery`

### 7. `VulkanBlitter`（240 行）

- `Blit`：图像拷贝（`vkCmdBlitImage` / `vkCmdCopyImage`）
- `Resolve`：MSAA 解析
- 依赖 `VulkanTexture` / `VulkanFboCache` / `VulkanCommands`

### 8. `VulkanReadPixels`（504 行）

- 独立的读回线程（`std::thread` + `std::condition_variable` + `std::queue`）
- `ReadPixels(...)` / `ReadTexture(...)` / `ReadBufferSubData(...)`
- 依赖 `math::vec4`

### 9. 资源对象追加到 `VulkanHandle.h`

- `VulkanProgram`（见第 1 项）
- `VulkanRenderPrimitive`：持 `VulkanVertexBufferPtr` / `VulkanIndexBufferPtr` / `VkPrimitiveType`

## Capabilities

### New Capabilities

- `vulkan-pipeline`: `VulkanAsyncHandles` / `VulkanPipelineCache` / `VulkanPipelineLayoutCache` / `VulkanDescriptorSetLayoutCache` / `VulkanDescriptorSetCache` / `VulkanRenderPrimitive` 与类型表登记
- `vulkan-query-blit-readback`: `VulkanQueryManager` / `VulkanBlitter` / `VulkanReadPixels`

### Modified Capabilities

- `vulkan-resource`: 类型表追加 6 条特化与 6 条销毁分支，并首次运行 `ThreadSafeResource` 的双队列路径

## Impact

- 新增：
  - `src/vulkan/VulkanAsyncHandles.h` / `.cpp`
  - `src/vulkan/VulkanPipelineCache.h` / `.cpp`
  - `src/vulkan/VulkanPipelineLayoutCache.h` / `.cpp`
  - `src/vulkan/VulkanDescriptorSetCache.h` / `.cpp`
  - `src/vulkan/VulkanDescriptorSetLayoutCache.h` / `.cpp`
  - `src/vulkan/VulkanQueryManager.h` / `.cpp`
  - `src/vulkan/VulkanBlitter.h` / `.cpp`
  - `src/vulkan/VulkanReadPixels.h` / `.cpp`
- 修改：
  - `src/vulkan/VulkanHandle.h` / `.cpp`（+`VulkanProgram` / `VulkanRenderPrimitive`）
  - `src/vulkan/resource/Resource.h` / `Resource.cpp`（+特化 ×6）
  - `src/vulkan/resource/ResourceManager.cpp`（+销毁分支 ×6，分支数 12 → 18）
- 不改：`src/vulkan/sync/VulkanCmdFence.{h,cpp}`（本项目已有，`VulkanAsyncHandles.cpp` 中的同名实现不移植）
- 不改：`DriverAPI.inc`（本变更为组件层，方法随变更 7 追加）
- 验证：`bin/BackendTests` 不回归；新增 `VulkanProgram` 的构造/销毁（触发 `ThreadSafeResource` 双队列路径）+ 管线缓存的创建/命中/Gc 往返

**最高风险**：`VulkanPipelineCache` 与 `VulkanDescriptorSetCache` 的 `tsl::robin_map` → `std::unordered_map` 替换（design D2），以及 825 行管线状态机的逐字段对齐（design D3）。
