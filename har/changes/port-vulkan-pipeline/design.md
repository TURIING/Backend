# Design: port-vulkan-pipeline

## Context

本变更是全部变更中最大的一块（约 3200 行），也是唯一同时具备三个「第一次」的变更：

1. **第一次让 `ThreadSafeResource` 有实际使用者**——变更 1 建立的双 GC 队列分类表此前无运行时验证点，`VulkanProgram` / `VulkanFence` / `VulkanSync` / `VulkanTimerQuery` 是本变更引入的四个使用者。
2. **第一次替换高风险 `robin_map`**——变更 5 已处理 `VulkanFboCache`，本变更要处理 `VulkanPipelineCache`（825 行，含 `Gc()`）与 `VulkanDescriptorSetCache`，是变更 1 design D7 点名的重点。
3. **第一次引入线程**——`VulkanReadPixels` 自带读回线程，`CompilerThreadPool`（变更 1 就位但无调用方）在本变更首次被 `VulkanPipelineCache` 使用。

上游依赖关系：

```
  VulkanAsyncHandles（VulkanProgram / PushConstantDescription）
        │
        ├──────────────► VulkanPipelineLayoutCache（需要 Program 的 push constant 范围）
        │                        │
        ▼                        ▼
  VulkanPipelineCache ───────────┘
        │  BindProgram / BindLayout / BindPipeline
        │
        ├──────────────► VulkanDescriptorSetLayoutCache
        │                        │
        ▼                        ▼
  VulkanDescriptorSetCache（Commit 时调 vkCmdBindDescriptorSets）
        │
        ▼
  VulkanBlitter / VulkanReadPixels / VulkanQueryManager
```

## Goals / Non-Goals

**Goals:**

- `VulkanProgram` 可从 `backend::Program` 构造出 `VkShaderModule`，push constant 可写
- `VulkanPipelineCache` 可从管线状态创建 `VkPipeline`，缓存命中正确，`Gc()` 可回收
- `VulkanDescriptorSet(Layout)Cache` 可创建与绑定
- `VulkanQueryManager` / `VulkanBlitter` / `VulkanReadPixels` 可独立构造与销毁
- **`ThreadSafeResource` 双队列路径首次得到运行时验证**
- `bin/BackendTests` 不回归

**Non-Goals:**

- 不接线 `VulkanDriver` 的 `createProgramR` / `bindPipeline` / `draw` / `beginTimerQuery` / `readPixels` / `blit` / `resolve`（属变更 7）
- 不追加任何 `DriverAPI.inc` 方法声明
- 不移植 `VulkanCmdFence`（本项目 `src/vulkan/sync/VulkanCmdFence.h` 已有）
- 不移植 `VulkanExternalImageManager` / `VulkanStreamedImageManager`（用户决策砍掉）
- 不实现 `VulkanStream` 相关路径
- 不为 `VulkanReadPixels` 提供跨平台线程优先级设置（MoltenVK 无对应需求）

## Decisions

### D1: `VulkanPipelineCache` 的并行编译路径——保留而非裁剪

上一轮探索中曾建议「砍掉异步，删掉 `AsyncPrewarmCache` 与 `AddCachePrewarmCallback`，因为它 include 了 `CallbackManager.h` + `CompilerThreadPool.h`」。用户随后明确要求移植 `VulkanAsyncHandles`，而变更 1 已把 `CallbackManager` / `CompilerThreadPool` / `CallbackHandler` 全部就位。

**决策**：**保留**并行编译路径，不裁剪 `VulkanPipelineCache`。理由：

1. 变更 1 已付出建立异步底座的成本；裁剪意味着这部分成本白付。
2. `createProgramR`（变更 7）的 pipeline prewarming 分支依赖它；裁剪会导致该分支需要改写，破坏「不改变实现」。
3. 保留后，`VulkanPipelineCache` 可逐行对齐上游，无需任何人为删减。

**`utils::JobSystem` 的处理**：`VulkanPipelineCache.cpp` 第 86/88 行调用 `JobSystem::setThreadName` / `setThreadPriority`，映射到变更 1 的 `NS_BD::JobSystem::SetThreadName` / `SetThreadPriority`（macOS 下前者实现 `pthread_setname_np`，后者空实现）。

### D2: `robin_map` → `unordered_map` 的三处核对

本变更的三个使用文件：

| 文件 | `erase` 调用 | 风险 |
|---|---|---|
| `VulkanPipelineCache` | `Gc()` + `ResetBoundPipeline()` | **高** |
| `VulkanPipelineLayoutCache` | 无（只增） | 低 |
| `VulkanDescriptorSetLayoutCache` | 无（只增） | 低 |
| `VulkanDescriptorSetCache` | 待核对 | 中 |

**决策**：替换前 SHALL 对每个文件逐行核对以下三问（与变更 5 的 D4 同一清单）：

1. 是否在遍历中按 key 删除当前元素？（`unordered_map` 下 `map.erase(key)` 会失效当前迭代器；`it = map.erase(it)` 是合法写法）
2. 回调（如 `VulkanProgram` 析构触发的 GC、`CallbackManager` 派发）是否在遍历期间修改容器？
3. 是否跨 rehash 持有迭代器或引用？（`unordered_map` rehash 时引用稳定、迭代器失效）

**`VulkanPipelineCache` 的两个重点函数**：
- `Gc()`：遍历管线缓存删除未被引用的条目
- `ResetBoundPipeline()`：重置当前绑定的管线状态（可能触发重建）

**处置顺序**：先修正遍历写法（若需），再替换容器。顺序不可颠倒。

**性能记录要求**：管线缓存是热路径，替换后 SHALL 记录实测的查找性能差异（哪怕只是定性结论）。变更 1 的 design D7 承诺了这一点。

### D3: `VulkanPipelineCache` 的状态机是逐字段契约

`RasterState`（本地结构，非变更 2 的 `RasterState`）由 `VulkanDriver::bindPipelineImpl` 逐字段填充：

```cpp
VulkanPipelineCache::RasterState const vulkanRasterState{
    .cullMode = VK_UTILS::GetCullMode(rasterState.culling),
    .frontFace = VK_UTILS::GetFrontFace(rasterState.inverseFrontFaces),
    .depthBiasEnable = (depthOffset.constant || depthOffset.slope) ? true : false,
    .blendEnable = rasterState.hasBlending(),
    // ... 共 20 个字段
};
```

**决策**：`VulkanPipelineCache::RasterState` 的字段名、类型、顺序 SHALL 逐字段对齐上游。同理 `PipelineKey` 与 `VulkanPipelineCache::PipelineEqual`。

**理由**：`bindPipelineImpl` 用指定初始化器（designated initializers）逐字段构造，**字段顺序与名称是编译期契约**——任何偏差都会在变更 7 引发连锁改写。这是本变更最容易被低估的风险点。

**额外约束**：`PipelineEqual` / `PipelineHashFn` 是 `unordered_map` 的模板参数，须与容器替换一起核对；若上游 `PipelineEqual` 依赖 `robin_map` 的特定行为（例如比较时的短路顺序），须记录。

### D4: `VulkanProgram` 落位 `VulkanAsyncHandles.h`，但类型表归本变更

上游 `VulkanProgram` 定义在 `VulkanAsyncHandles.h`（不在 `VulkanHandles.h`），而 `VulkanRenderPrimitive` 定义在 `VulkanHandles.h`。

**决策**：
- `VulkanProgram` / `PushConstantDescription` / `VulkanFence` / `VulkanSync` / `VulkanTimerQuery` 保持在 `src/vulkan/VulkanAsyncHandles.h`（与上游一致）
- `VulkanRenderPrimitive` 追加到既有 `src/vulkan/VulkanHandle.h`（本项目「非纹理 Hw 资源类型」的归属地，变更 5 已确立）

**理由**：保持上游文件归属可让对照成本最低；`VulkanRenderPrimitive` 放 `VulkanHandle.h` 与本项目既有落位一致。

### D5: `VulkanCmdFence` 不重复移植

上游 `VulkanAsyncHandles.{h,cpp}` **同时包含** `VulkanCmdFence` 的定义与实现（第 130-260 行）。本项目已在 `src/vulkan/sync/VulkanCmdFence.{h,cpp}` 有完整实现（探索阶段逐行比对确认：`Completed()` / `MarkSubmitted()` / `RefreshStatus()` / `GetStatus()` / `Wait()` / `Cancel()` / `SwapRecycleFn()` 语义一致）。

**决策**：本变更 SHALL NOT 移植 `VulkanCmdFence`。`VulkanAsyncHandles.h` 改为 include 既有的 `vulkan/sync/VulkanCmdFence.h`。

**核对项**：`VulkanFence` / `VulkanTimerQuery` / `VulkanSync` 持有的 `std::shared_ptr<VulkanCmdFence>` 须解析到既有类型。tasks 中列为复核项。

### D6: `VulkanReadPixels` 的线程模型

上游 `VulkanReadPixels` 自带一个读回线程：

```cpp
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
```

**决策**：完整移植线程模型，`std::thread` + `std::condition_variable` + `std::queue` 直接使用。

**生命周期约束**（须以注释写明）：读回线程 SHALL 在 `VulkanReadPixels` 析构前 join；`VulkanDriver` 的 `DestroyResources()` 须在 `vmaDestroyAllocator` **之前**销毁 `VulkanReadPixels`（否则读回线程可能访问已释放的 allocator）。该约束须在变更 7 的 design 中引用。

### D7: `ThreadSafeResource` 的运行时验证点

变更 1 建立了双 GC 队列但无调用方。本变更引入四个 `ThreadSafeResource` 派生类型，其归零入队路径：

```
VulkanProgram 引用归零
    → Resource::OnLastRef()
    → ResourceManager::DestructLaterWithType(ResourceType::Program, id)
    → isThreadSafeType(Program) == true
    → m_threadSafeGcList 入队（持 m_threadSafeGcListMutex）
    → 下一次 Gc() 排空线程安全队列
    → DestroyWithType(Program, id) → destruct<VulkanProgram>
```

**决策**：本变更的验证须**显式覆盖这条路径**——创建 `VulkanProgram` → 释放引用 → 调用 `Gc()` → 断言对象已析构且池块已归还。这是变更 1 所记录的「运行时验证留待变更 6」的兑现点。

**并发验证**：`VulkanPipelineCache` 的并行编译路径会在编译线程上释放 `VulkanProgram` 引用。理想情况下应验证「编译线程归零 + backend 线程 `Gc()`」的并发入队。但该路径需要真实的管线编译，本变更可能无法在测试中触发。**处置**：若无法触发，在 tasks 中记录该并发场景「未运行时验证」，并以代码走查 + 变更 1 的锁范围复核结论为准。

### D8: 验证策略

1. **全量构建** + `bin/BackendTests` 不回归
2. **`VulkanProgram` 构造/销毁往返**：以最小 SPIR-V 二进制构造 `backend::Program` → `VulkanProgram` → 释放 → `Gc()` → 断言析构与池块归还（**兑现 D7**）
3. **管线缓存往返**：相同 `PipelineKey` 两次 `GetOrCreatePipeline` 返回同一 `VkPipeline`；不同 key 返回不同；引用归零后 `Gc()` 使旧项回收
4. **`VulkanQueryManager` 往返**：`AcquireTimerQuery` → `ReleaseTimerQuery` → `Terminate`
5. **`VulkanReadPixels` 构造/析构**：验证读回线程正确 join（析构不挂起）

**固有局限**：
- `VulkanBlitter` 的 `Blit` / `Resolve` 需要真实命令录制与渲染目标，本变更**不验证**运行期行为
- `VulkanReadPixels` 的实际读回需要渲染目标与 `beginRenderPass`，本变更**只验证构造/析构**
- 管线创建需要着色器模块；若测试中无有效 SPIR-V，`CreatePipeline` 路径**不验证**，只验证状态机与缓存键

## Risks / Trade-offs

- [`robin_map` → `unordered_map` 在 `VulkanPipelineCache::Gc()` 引入迭代器失效 UB] → D2 的三问核对清单 + 「先修遍历再换容器」的顺序约束
- [`VulkanPipelineCache::RasterState` 字段顺序偏差在变更 7 才暴露] → D3 要求逐字段对齐；tasks 单列复核项，对照 `bindPipelineImpl` 的指定初始化器逐个字段验证
- [`VulkanProgram` 的 `ThreadSafeResource` 适配遗漏] → D7 的显式验证；若遗漏，`isThreadSafeType` 分类正确但构造路径未走 `ThreadSafeResource::Init`，会导致元数据未绑定
- [`VulkanCmdFence` 重复定义] → D5 明确不移植；核对 `VulkanAsyncHandles.h` include 既有头文件
- [并行编译路径引入线程，测试难以覆盖] → D7 记录未验证面；`CompilerThreadPool` 的正确性依赖变更 1 的实现质量
- [`VulkanReadPixels` 线程与 `VulkanDriver::DestroyResources` 的销毁顺序] → D6 记录生命周期约束；变更 7 必须遵守
- [本变更体量大（约 3200 行），单次落地风险集中] → 内部按组件分 6-8 个可独立编译的批次推进；tasks 按组件分组
- [管线缓存性能退化] → D2 要求记录实测差异；若退化明显，作为独立变更重新评估容器选型

## Open Questions

- `VulkanPipelineCache::PipelineKey` 的哈希是否依赖 `robin_map` 的特定行为：`PipelineHashFn` 若使用了上游 `utils::hash` 的组合方式，须映射到变更 1 的 `NS_UTILS::HashCombine`，语义等价性需验证
- `VulkanDescriptorSetCache` 的 `Commit` 中 `DescriptorSetMask::ForEachSetBit` 遍历期间是否有容器修改：变更 4 的 `DescriptorSetMask` 基于 `NS_UTILS::Bitset32`，遍历语义与上游一致，但容器修改的并发性须核对
- `VulkanBlitter` 与 `VulkanReadPixels` 是否本变更必需：二者均未被「画三角形」的最小路径直接依赖（三角形不需要 blit 或读回），但用户明确要求移植。是否可拆为独立变更以降低本变更体量——倾向保持在一个变更内（用户已决定范围）
- `VulkanQueryManager` 的查询池容量与重置时机：上游在 `VulkanDriver::tick` 中重置，该接线属变更 7
- 并行编译的取消语义：`VulkanProgram::IsParallelCompilationCanceled` 在 `destroyProgram` 中置位，编译线程据此跳过。若 `CompilerThreadPool`（变更 1）的任务排队语义与上游有差异，此处会静默失效——须在变更 6 落地时核对

## Open Questions 实施回填

### 1. `PipelineHashFn` 的哈希等价性

**等价。** 上游 `utils::hash::MurmurHashFn<T>` 的实现是 `murmur3(reinterpret_cast<uint32_t const*>(&key), sizeof(key) / 4, 0)`；本项目
`NS_UTILS::hash::MurmurHashFn<T>`（`3rd/Utils/include/Utils/Hash.h`）逐行相同（同样的乘法常量、同样的 `wordCount` 收尾、同样的 3 轮雪崩），
且同样以 seed 0、按 `sizeof(T)/4` 个字求哈希。因此 `PipelineKey` / `PipelineLayoutKey` / `LayoutKey` 的哈希值两边一致，
`PipelineEqual`（memcmp）的等值语义亦与 `robin_map` 的 `PipelineEqual` 相同。哈希不依赖 `robin_map` 的任何行为。

### 2. `DescriptorSetCache::Commit` 遍历期间是否有容器修改

**没有。** 三处 `curMask.ForEachSetBit` 遍历的都是栈上 `DescriptorSetMask` 的**值拷贝**；回调内只做两类动作：
`curMask.Unset(index)`（只影响当前位，且本项目 `Bitset::ForEachSetBit` 先把整字读进局部变量再遍历，故不破坏本次遍历）与
`commands->Acquire(set)`（修改的是命令缓冲的借用列表，不是本缓存的容器）。`m_stashedSets` / `m_lastBoundInfo` 的赋值全部发生在遍历**之后**。

### 3. `VulkanBlitter` / `VulkanReadPixels` 是否本变更必需

按用户决策保持在变更内，未拆分。

### 4. `VulkanQueryManager` 的查询池容量与重置时机

上游**没有**「池满时新建 `VkQueryPool`」的逻辑，也没有 `reset()`：池容量固定为 `~bitset32` 的 32 个计时器（64 个查询），
池满时打一条 `LOG_ERROR` 并返回空；`mUsed` 的位在 `clearQuery` 中释放。逐帧重置并不存在（`VulkanDriver::tick` 不触碰它），
`mTiming.lock` 是交换链时序用的另一把锁。变更 7 接线时无须为本项预留重置调用。

### 5. 并行编译的取消语义

`ProgramToken`（变更 1）与上游 `ProgramToken` 语义一致：`std::shared_ptr<ProgramToken>` 与任务一同入队，
`CompilerThreadPool::Dequeue(token)` 按 token 摘除尚未执行的任务。`AsyncPrewarmCache` 的任务体内先查
`vprogram->IsParallelCompilationCanceled()` 再编译，取消路径与上游逐行一致。
**未运行时验证**：预编译需要 `VulkanContext::IsPipelineCachePrewarmingEnabled()` 为真（由 `DriverConfig::vulkanEnableAsyncPipelineCachePrewarming`
决定，本项目默认关闭），测试无法在不改动驱动配置的前提下触发该路径。

