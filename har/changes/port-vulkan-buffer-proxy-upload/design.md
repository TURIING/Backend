# Design: port-vulkan-buffer-proxy-upload

## Context

上游 `VulkanBufferProxy` 是 CPU→GPU 上传的唯一通道：`loadFromCpu` 先判断能否直接写映射内存（UMA + staging 直通开关，或 usage 标了 STATIC/SHARED_WRITE），否则取暂存段 + `vkCmdCopyBuffer`，并在同命令缓冲内"读后又写"时插屏障；`referencedBy` 与 `mLastReadAge` 配合完成这个判定。

本项目在 `port-vulkan-buffer` 中把这两件事按用户决策砍掉了，结果是：`VulkanBufferObject::LoadFromCpu`（`src/vulkan/VulkanHandle.cpp:124`）至今是空桩、`VulkanIndexBuffer` 没有该方法、`mLastReadAge` 不存在。本次按用户决策只补**代理层**，让通道具备能力；调用方（`BufferDescriptor` / `Driver::scheduleDestroy` / 驱动 `update*` 接口）留待后续变更。

已有的可复用件：`VulkanStagePool::AcquireStage(numBytes, alignment = 0)`、`VulkanStageBuffer::Segment` 的 `GetMapping()` / `GetOffset()` / `GetVkBuffer()` / `GetMemory()`、`VulkanCommandBuffer::Acquire(SharedPtr<Resource>)` / `Age()` / `Buffer()`、`VulkanContext::IsStagingBufferBypassEnabled()`、VMA 的 flush。缺的只有 `BufferUsage` 位运算。

## Goals / Non-Goals

**Goals:**
- 代理层上传能力完整：直写与 staging 两条路径、按 binding 映射的读/写屏障、借用登记
- 构造签名与上游语义对齐（回补 `VulkanStagePool`），本地形态用 `Ptr` 而非引用
- 构建与既有测试零回归

**Non-Goals:**
- 不引入 `BufferDescriptor`、`Driver::scheduleDestroy`、驱动 `updateBufferObject` / `updateIndexBuffer`（含异步变体与 `CallbackHandler`）
- 不把 `VulkanBufferObject::LoadFromCpu` 空桩转正，不为 `VulkanIndexBuffer` 暴露 `LoadFromCpu`
- 不移植上游 `utils::EnableBitMaskOperators` 模板设施

## Decisions

### D1: `m_stagePool` 用 `VulkanStagePoolPtr` 而非上游的引用

上游成员是 `VulkanStagePool&`，但本项目 `VulkanBufferCache` 已是 `VulkanBufferCachePtr`，`VulkanDriver` 也以 `VulkanStagePoolPtr` 持有暂存池。沿用本地惯例用 `VulkanStagePoolPtr`（构造参数 `const VulkanStagePoolPtr&`），保持"驱动层用 SharedPtr 持有子系统"的一致形态，避免为对齐上游引入引用成员（还得处理生命周期顺序）。

### D2: `BufferUsage` 位运算用最小本地方案

上游经 `utils::EnableBitMaskOperators<BufferUsage>` 模板特化拿到 `operator|`/`operator&`，再用 `any(...)` 判定；本项目 `3rd/Utils` 没有该设施（只有 `BIT(x)` 宏）。为一个枚举引入模板设施不划算，改为在 `include/Backend/DriverDefine.h` 直接定义：

```cpp
constexpr BufferUsage operator|(BufferUsage lhs, BufferUsage rhs) noexcept;
constexpr BufferUsage operator&(BufferUsage lhs, BufferUsage rhs) noexcept;
constexpr bool HasAnyFlag(BufferUsage value, BufferUsage flags) noexcept;
```

其中 `HasAnyFlag` 取代上游的 `any(mUsage & flags)`。不提供 `operator bool`：`STATIC` 恰为 0，`if (usage)` 会把 STATIC 判成"无标志"。

### D3: 命名与访问器映射

| 上游 | 本地 |
|---|---|
| `loadFromCpu` / `referencedBy` | `LoadFromCpu` / `ReferencedBy` |
| `mBuffer->getCount()`（handle 计数） | `m_buffer->GetRefCount()` |
| `context.stagingBufferBypassEnabled()` | `context->IsStagingBufferBypassEnabled()` |
| `mStagePool.acquireStage(n)` | `m_stagePool->AcquireStage(n)` |
| `stage->memory()/mapping()/offset()/buffer()` | `stage->GetMemory()/GetMapping()/GetOffset()/GetVkBuffer()` |
| `commands.acquire(x)` / `age()` / `buffer()` | `commands.Acquire(x)` / `Age()` / `Buffer()` |

`getCount()` → `GetRefCount()` 是本次唯一语义映射需要留意的点：上游的 count 是 `Resource` 自己的句柄引用计数，本项目的 `VulkanBuffer` 是经 `AllocateAndConstruct` 创建的内部资源（无句柄引用），其拥有者就是代理持有的那个 `SharedPtr`，计数 1 等价于"仅代理持有、GPU 未在用"。命令缓冲借用后计数 +1，与上游语义一致。

### D4: 屏障逻辑照搬上游，仅按本地枚举名改写

`VkAccessFlags` / `VkPipelineStageFlags` 的取值与 binding 分支完全照搬，不做"优化"——这套映射（Index 用 `VK_ACCESS_INDEX_READ_BIT` + `VK_PIPELINE_STAGE_VERTEX_INPUT_BIT` 等）是上游踩过坑的结果，改错不会编译报错、只会在运行期表现为偶发花屏/数据错乱。`ShaderStorage` 分支上游留 TODO，本地同样不追加。

### D5: 本次有意产出暂未调用的方法

`LoadFromCpu` / `ReferencedBy` 在本次之后仍无调用方（资源层转发与驱动接口不在范围内），属**有意的死代码**。取舍记录：

- 反方：违反"不留无用代码"的一般直觉，且无法被运行期验证
- 正方：这两个方法与其依赖的 `m_stagePool` / `m_lastReadAge` 是同一组状态，拆到后续变更会让代理类连着改两次签名；本次一并落地后，后续变更只需加 `BufferDescriptor`、`scheduleDestroy` 与驱动接口，代理层零改动
- 缓解：在 tasks 中单列复核项（与上游逐行对照），并在 proposal / design 明确"暂无调用方"这一事实，避免后来者误以为已接通

`VulkanBufferObject::LoadFromCpu` 空桩本次**保持不动**（其注释"上传通道尚未移植"描述的正是本次范围之外的驱动侧）。

### D6: 构造签名连锁的三处改动

代理构造签名多一个参数，牵动两个资源的构造函数与驱动的两个创建点：

```
VulkanDriver::CreateBufferObjectR ─┐
                                   ├─► Make<VulkanBufferObject>(..., m_stagePool, m_bufferCache, ...)
VulkanDriver::CreateIndexBufferR  ─┘   Make<VulkanIndexBuffer>(..., m_stagePool, m_bufferCache, ...)
```

参数顺序统一为 `(context, allocator, stagePool, bufferCache, ...)`，即"上下文 → 显存分配器 → 暂存池 → 缓冲池 → 业务参数"。这两个创建路径被 `BackendTests` 的索引缓冲往返覆盖，签名改错会在运行期立刻暴露。

## Risks / Trade-offs

- [暂未调用的方法与成员] → 见 D5；成员函数不触发 `-Wunused`，`m_lastReadAge` 被 `LoadFromCpu`/`ReferencedBy` 读写故非未使用变量
- [`GetRefCount() == 1` 判定与上游 `getCount() == 1` 不等价于同一计数器] → 语义已按 D3 对齐：两者都表示"除本代理外无其他借用者"，命令缓冲借用会同时抬高两个计数
- [屏障映射写错不会编译报错] → 照搬上游不改写；tasks 单列对照复核项
- [构造签名变更影响既有创建路径] → 由 `BackendTests` 索引缓冲往返（16/32 位各一次）覆盖，构建 + 运行双验证
- [`BufferUsage` 不引入上游模板设施，后续枚举需位运算时可能重复定义] → 每个枚举各自定义一组 `constexpr` 运算符，`HasAnyFlag` 按类型重载即可；若第三个枚举出现同类需求，再抽公共设施

## Open Questions

- 后续变更的边界：`BufferDescriptor` + `Driver::scheduleDestroy` + 驱动 `updateBufferObject` / `updateIndexBuffer` 是否一次做完，还是先做同步路径、异步链（`CallbackHandler`）单独一次
- `VulkanBufferObject::LoadFromCpu` 空桩转正与 `VulkanIndexBuffer::LoadFromCpu` 暴露，应随驱动 `update*` 接口一起做（否则仍是无人调用的转发）
