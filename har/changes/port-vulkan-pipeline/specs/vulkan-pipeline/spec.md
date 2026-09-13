# Capability: vulkan-pipeline

## Purpose

Vulkan 图形管线资源层：着色器模块与程序对象（`VulkanProgram`）、push constant 写入、管线状态机与 `VkPipeline` 缓存（`VulkanPipelineCache`）、管线布局缓存（`VulkanPipelineLayoutCache`）、描述符集布局与描述符集缓存（`VulkanDescriptorSetLayoutCache` / `VulkanDescriptorSetCache`）、渲染图元对象（`VulkanRenderPrimitive`）。上游对应约 2260 行。

本能力域还承担 `ThreadSafeResource` 的首次运行时验证——`VulkanProgram` / `VulkanFence` / `VulkanSync` / `VulkanTimerQuery` 是变更 1 所建双 GC 队列分类表的首批使用者。

## ADDED Requirements
### Requirement: VulkanAsyncHandles 五个类型

`src/vulkan/VulkanAsyncHandles.{h,cpp}` SHALL 定义：

- `PushConstantDescription`：从 `backend::Program` 构建 push constant 范围，`Write(VkCommandBuffer, VkPipelineLayout, ShaderStage, uint8_t, PushConstantVariant const&)` 写入
- `VulkanProgram`：public 继承 `HwProgram` + `ThreadSafeResource`；持 `VkShaderModule` 数组（顶点/片元）、`PushConstantDescription`、待写入的 push constant 队列
- `VulkanFence`：public 继承 `HwFence` + `ThreadSafeResource`；持 `std::shared_ptr<VulkanCmdFence>`
- `VulkanSync`：public 继承 `ThreadSafeResource` + `HwSync`；持 `Platform::Sync*` + `std::vector<std::unique_ptr<CallbackData>>`
- `VulkanTimerQuery`：public 继承 `HwTimerQuery` + `ThreadSafeResource`；持起止查询下标 + `std::shared_ptr<VulkanCmdFence>` + `std::mutex`

四类型（除 `PushConstantDescription`）SHALL 继承 `ThreadSafeResource`——与上游 `fvkmemory::ThreadSafeResource` 一致。

**`VulkanCmdFence` SHALL NOT 在本文件重复定义**：本项目 `src/vulkan/sync/VulkanCmdFence.{h,cpp}` 已有完整实现，本文件 SHALL include 该头文件。

`utils::Mutex` SHALL 替换为 `std::mutex`；`std::atomic<bool>` 保留（`VulkanProgram::mParallelCompilationCanceled`）。

#### Scenario: 四个类型走线程安全构造路径

- **WHEN** 对 `VulkanProgram` / `VulkanFence` / `VulkanSync` / `VulkanTimerQuery` 分别执行 `Make<D, B>(handle)`
- **THEN** `ResourceManager::construct` 的 `if constexpr` 分派到 `ThreadSafeResource::Init<D>`，元数据绑定成功

#### Scenario: 归零入线程安全队列

- **WHEN** `VulkanProgram` 引用归零
- **THEN** `{ResourceType::Program, id}` 进入 `m_threadSafeGcList`（而非 `m_gcList`），`Gc()` 排空后对象析构、池块归还

#### Scenario: VulkanCmdFence 单一实现

- **WHEN** 全项目检索 `struct VulkanCmdFence`
- **THEN** 只有 `src/vulkan/sync/VulkanCmdFence.h` 一处定义

#### Scenario: push constant 写入

- **WHEN** 调用 `VulkanProgram::WritePushConstant(cmd, layout, stage, index, value)` 且 `layout != VK_NULL_HANDLE`
- **THEN** 立即经 `PushConstantDescription::Write` 写入命令缓冲

#### Scenario: layout 未就绪时排队

- **WHEN** 调用 `WritePushConstant` 且 `layout == VK_NULL_HANDLE`
- **THEN** 加入 `mQueuedPushConstants`，待 `FlushPushConstants(layout)` 时写出

### Requirement: VulkanProgram 资源对象

`VulkanProgram` SHALL 提供：

- 构造 `VulkanProgram(VkDevice, backend::Program const&)`：为每个 `ShaderStage` 创建 `VkShaderModule`
- 析构：销毁全部 `VkShaderModule`
- `GetVertexShader()` / `GetFragmentShader()` / `GetPushConstantRangeCount()` / `GetPushConstantRanges()`
- `CancelParallelCompilation()` / `IsParallelCompilationCanceled()`
- `FlushPushConstants(VkPipelineLayout)`
- `WritePushConstant(...)`
- `static constexpr uint8_t MAX_SHADER_MODULES = 2`

`Resource.h` SHALL 补 `GetTypeEnum<VulkanProgram>` 特化（返回 `ResourceType::Program`）。

#### Scenario: 着色器模块创建

- **WHEN** 以含顶点与片元 SPIR-V 的 `backend::Program` 构造
- **THEN** `GetVertexShader()` / `GetFragmentShader()` 均为有效 `VkShaderModule`

#### Scenario: 析构销毁模块

- **WHEN** `VulkanProgram` 析构
- **THEN** 全部 `VkShaderModule` 经 `vkDestroyShaderModule` 销毁

#### Scenario: 并行编译取消

- **WHEN** 调用 `CancelParallelCompilation()` 后查询 `IsParallelCompilationCanceled()`
- **THEN** 返回 true

### Requirement: VulkanPipelineCache 状态机

`src/vulkan/VulkanPipelineCache.{h,cpp}` SHALL 定义 `VulkanPipelineCache`，提供：

- 绑定接口：`BindProgram` / `BindRasterState` / `BindStencilState` / `BindRenderPass` / `BindPrimitiveTopology` / `BindVertexArray` / `BindLayout` / `BindPipeline(VulkanCommandBuffer*)`
- `GetOrCreatePipeline()` → `CreatePipeline(...)`：组装 `VkGraphicsPipelineCreateInfo` 并调 `vkCreateGraphicsPipelines`
- `ResetBoundPipeline()` / `Gc()` / `Terminate()`
- 并行编译：`AsyncPrewarmCache(...)` / `AddCachePrewarmCallback(CallbackHandler*, ...)`
- 嵌套 `struct RasterState`：逐字段对齐上游（约 20 个字段）
- `PipelineKey` / `PipelineEqual` / `PipelineHashFn`

`BindPipeline` SHALL 在管线未命中缓存时创建，命中时直接 `vkCmdBindPipeline`。

`Cursor` / 当前绑定状态 SHALL 在 `ResetBoundPipeline()` 中清空。

#### Scenario: 相同状态命中缓存

- **WHEN** 以相同状态序列两次绑定后 `GetOrCreatePipeline()`
- **THEN** 第二次不调用 `vkCreateGraphicsPipelines`，返回同一 `VkPipeline`

#### Scenario: 状态变化创建新管线

- **WHEN** 仅 `cullMode` 不同
- **THEN** 创建不同的 `VkPipeline`

#### Scenario: Gc 回收未引用管线

- **WHEN** 某管线引用归零后调用 `Gc()`
- **THEN** 该 `VkPipeline` 经 `vkDestroyPipeline` 销毁，再次以相同状态请求时重建

#### Scenario: RasterState 字段契约

- **WHEN** 以指定初始化器逐字段构造 `VulkanPipelineCache::RasterState`
- **THEN** 字段名、类型、顺序与上游逐字段一致，编译通过

### Requirement: VulkanPipelineLayoutCache

`src/vulkan/VulkanPipelineLayoutCache.{h,cpp}` SHALL 提供：

- `using DescriptorSetLayoutArray = std::array<VkDescriptorSetLayout, MAX_DESCRIPTOR_SET_COUNT>;`
- `GetLayout(DescriptorSetLayoutArray const&, VulkanProgramPtr const&) → VkPipelineLayout`
- 按 `VkDescriptorSetLayout` 数组 + push constant 范围组合缓存
- `Terminate()`

容器 SHALL 为只增缓存（无 `erase`），按映射约定替换为 `std::unordered_map`。

#### Scenario: 相同布局命中缓存

- **WHEN** 以相同布局数组两次调用 `GetLayout`
- **THEN** 返回同一 `VkPipelineLayout`

#### Scenario: push constant 范围参与缓存键

- **WHEN** 两个 program 的 push constant 范围不同，其余布局相同
- **THEN** 返回不同的 `VkPipelineLayout`

### Requirement: VulkanDescriptorSetLayoutCache

`src/vulkan/VulkanDescriptorSetLayoutCache.{h,cpp}` SHALL 提供：

- `CreateLayout(Handle<HwDescriptorSetLayout>, DescriptorSetLayout) → VulkanDescriptorSetLayoutPtr`
- `GetVkLayout(DescriptorSetMask, DescriptorSetMask externalSamplerMask, externalSamplers) → VkDescriptorSetLayout`
- `Terminate()`

`VulkanDescriptorSetLayout` 资源对象 SHALL 含 `bitmask`（`DescriptorSetMask`）与 `externalSampler`（`SamplerBitmask`）成员——`VulkanDriver::createProgramR` 用 `layout->bitmask.externalSampler.count() > 0` 判定是否有外部采样器。

#### Scenario: 布局创建

- **WHEN** 以 `DescriptorSetLayout` 描述调用 `CreateLayout`
- **THEN** 返回的 `VulkanDescriptorSetLayout` 含有效的 `VkDescriptorSetLayout` 与正确的 `bitmask`

#### Scenario: 按掩码组合缓存

- **WHEN** 以相同 `bitmask` 两次调用 `GetVkLayout`
- **THEN** 返回同一 `VkDescriptorSetLayout`

### Requirement: VulkanDescriptorSetCache

`src/vulkan/VulkanDescriptorSetCache.{h,cpp}` SHALL 提供：

- `Bind(descriptor_set_t setIndex, VulkanDescriptorSetPtr, DescriptorSetOffsetArray&& offsets)`
- `Unbind(descriptor_set_t setIndex)`
- `GetBoundSets() → DescriptorSetArray const&`
- `Commit(VulkanCommandBuffer*, VkPipelineLayout, DescriptorSetMask const&)`
- `Terminate()`

`Commit` SHALL 先按 `setMask` 与已 stash 的集合求交（未 stash 的位从掩码中清除），再跳过与上次绑定相同的集合（`uniqueDynamicUboCount == 0` 时），最后对剩余位调 `vkCmdBindDescriptorSets` 并 `commands->Acquire(set)`。

`VulkanDescriptorSet` 资源对象 SHALL 含 `boundLayout` / `isLayoutDirty` / `isAnExternalSamplerBound` / `uniqueDynamicUboCount` / `getVkSet()` / `getOffsets()`。

#### Scenario: 未绑定的位被跳过

- **WHEN** `setMask` 含下标 0 但该位未 stash
- **THEN** `Commit` 不对下标 0 调用 `vkCmdBindDescriptorSets`

#### Scenario: 重复绑定被跳过

- **WHEN** 同一集合与上次绑定相同且 `uniqueDynamicUboCount == 0`
- **THEN** 该位从掩码清除，不重复调用 `vkCmdBindDescriptorSets`

#### Scenario: 实际绑定后登记借用

- **WHEN** 对某位执行绑定
- **THEN** 该 `VulkanDescriptorSet` 经 `commands->Acquire(set)` 登记借用，提交完成前不被回收

### Requirement: VulkanRenderPrimitive 资源对象

`src/vulkan/VulkanHandle.h` / `.cpp` SHALL 追加 `VulkanRenderPrimitive`：

- public 继承 `HwRenderPrimitive` + `Resource`
- 持 `VulkanVertexBufferPtr vertexBuffer` / `VulkanIndexBufferPtr indexBuffer` / `VkPrimitiveType type`
- `Resource.h` SHALL 补 `GetTypeEnum<VulkanRenderPrimitive>` 特化（返回 `ResourceType::RenderPrimitive`）

`VulkanDriver::draw` 用 `rp->type` 覆盖 `state.primitiveType`、用 `rp->vertexBuffer->vbi` 推导 `vertexBufferInfo`——两处访问的字段名与类型是契约。

#### Scenario: 无索引缓冲的图元

- **WHEN** 构造 `VulkanRenderPrimitive` 时 `indexBuffer` 为空
- **THEN** `bindRenderPrimitive` 跳过 `vkCmdBindIndexBuffer`（非索引绘制路径）

#### Scenario: 类型字段可读

- **WHEN** 访问 `prim->type` 与 `prim->vertexBuffer->vbi`
- **THEN** 编译通过，语义与上游 `VulkanDriver::draw` 的读取点一致

### Requirement: 适配约束
- `VulkanAsyncHandles.{h,cpp}` / `VulkanPipelineCache.{h,cpp}` / `VulkanPipelineLayoutCache.{h,cpp}` / `VulkanDescriptorSetLayoutCache.{h,cpp}` / `VulkanDescriptorSetCache.{h,cpp}` SHALL 位于 `src/vulkan/`
- `VulkanRenderPrimitive` SHALL 追加到既有 `src/vulkan/VulkanHandle.h` / `.cpp`
- `tsl::robin_map` SHALL 替换为 `std::unordered_map`，**保留自定义 Hash 与 Equal**（`PipelineHashFn` / `PipelineEqual`）
- **`erase` 语义核对**：替换前 SHALL 逐行核对 `Gc()` / `ResetBoundPipeline()` 的遍历结构，确认无「遍历中按 key 删除」与「回调期间修改容器」；若存在 UB 模式，**先修正遍历再替换容器**
- `utils::Invocable` → `std::function`、`utils::Condition` → `std::condition_variable`、`utils::Mutex` → `std::mutex`、`utils::FixedCapacityVector` → `std::vector`
- `utils::JobSystem::setThreadName` / `setThreadPriority` SHALL 映射到 `NS_BD::JobSystem::SetThreadName` / `SetThreadPriority`
- `fvkutils::` SHALL 映射到 `VK_UTILS::`（变更 4 的命名空间约定）
- `Resource.h` / `Resource.cpp` / `ResourceManager.cpp` SHALL 补 6 条特化与 6 条销毁分支（`Program` / `Fence` / `Sync` / `TimerQuery` / `DescriptorSetLayout` / `DescriptorSet` / `RenderPrimitive` 中本变更引入的部分）
- 上游文件头注释与 license 注释 SHALL 删除；`using namespace bluevk;` SHALL 删除
- 注释 SHALL 遵循 `.dsh/rules/code-style.md`：对非平凡逻辑（管线状态机的缓存键构造、`Commit` 的掩码求交、并行编译的取消时机）说明意图；单函数体内不超过 3 条

#### Scenario: 编译通过

- **WHEN** 构建整个 `Backend` 静态库
- **THEN** 本能力域的全部源文件编译通过，不依赖 Filament 任何头文件

#### Scenario: 命名与风格合规

- **WHEN** 检查本能力域新增的类型与函数
- **THEN** 命名遵循项目规范（`PascalCase` 类型与公有方法、`m_camelCase` 私有成员、`kPascalCase` 常量），头文件使用 `#pragma once`，不保留上游 license / 文件头注释
