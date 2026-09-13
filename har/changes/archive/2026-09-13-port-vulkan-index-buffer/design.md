# Design: port-vulkan-index-buffer

## Context

上游 `VulkanDriver.cpp` 中 IndexBuffer 由 7 个触点构成：`createIndexBufferS/AsyncS`（发句柄）、`createIndexBufferR/AsyncR`（建资源对象）、`destroyIndexBuffer`、`updateIndexBuffer/AsyncS/R`（上传）、`createRenderPrimitiveR`（持有引用）、draw 站点的 `vkCmdBindIndexBuffer`、以及资源层类型表登记。本次范围（用户决策：方案 A）只做**创建 + 销毁闭环**，上传与绘制路径整体不移植。

本项目已有可复用的地基：`VulkanBufferProxy`/`VulkanBufferCache`（索引缓冲直接复用）、`VulkanCommandBuffer`（`Acquire`/`Age`）、`VulkanStagePool`（driver 已持有，本次不用）、`IndexBufferHandle` 与 `ElementType`（公共定义已就位）、`ResourceType::IndexBuffer`（枚举已预留 = 1）。缺失的是：`HwIndexBuffer` 结构、`VulkanIndexBuffer` 资源对象、`Driver::GetElementTypeSize`、2 条驱动接口、以及类型表在缓冲族上的收口。

关键背景缺陷（探索阶段发现，见 D5）：`ResourceManager::Make` 返回的 `SharedPtr` 是唯一引用，driver 方法末尾析构即计数归零、对象被排入 GC 队列；而 `ResourceManager::Gc()` 目前仅在 `Terminate()` 中被调用，所以缺陷处于休眠状态。类型表分支一旦补齐，这些对象在下一次 `gc()` 时会被真正析构——必须先修引用语义，否则等于埋下 use-after-free。

## Goals / Non-Goals

**Goals:**
- 打通"客户端句柄 → `VulkanIndexBuffer` → `VulkanBufferProxy` → `VulkanBufferCache` → GC → 池块归还"完整闭环
- `HwIndexBuffer` 位域形态与 `VkIndexType` 推导与上游一致
- `vulkan-resource` 类型表在缓冲族上收口（4 类特化 + 4 类销毁分支全部可达）
- 确立"句柄持有一份引用"的构造/销毁语义，使 `Make`/`Destroy` 成为真正的成对 API
- 经测试工程跑通一次创建-销毁往返（命令流 → 驱动 → 资源层 → 终止期 GC）

**Non-Goals:**
- 不移植 `updateIndexBuffer` / `updateIndexBufferAsync`，不引入 `BufferDescriptor` / `Driver::scheduleDestroy`
- 不移植 `VulkanBufferProxy::LoadFromCpu` / `ReferencedBy`，不回补 `VulkanStagePool` 参数
- 不接线 `VulkanCommands` 到 `VulkanDriver`（`mCommands` 成员仍缺），不在帧循环泵 `ResourceManager::Gc()`
- 不移植 `VulkanRenderPrimitive` 与 `vkCmdBindIndexBuffer` 绘制路径
- 不为 `VulkanHandle.h` 其余类型（Texture/RenderTarget/Program 等）补类型表

## Decisions

### D1: `HwIndexBuffer` 落在 `src/HwDefine.h`，位宽用文件内常量

`Hw*` 资源结构在本项目统一定义于 `src/HwDefine.h`（`HwVertexBufferInfo` / `HwVertexBuffer` / `HwBufferObject` 已在此），`HwIndexBuffer` 顺此落位，不新建文件。位宽以 `constexpr uint32_t kIndexCountBits = 26;`、`constexpr uint32_t kElementSizeBits = 5;` 表达并在位域声明中引用，满足"魔法数字不散落"；这两个常量仅服务本结构，属功能私有定义，不上升为通用常量。

```cpp
struct HwIndexBuffer : public HwBase {
    uint32_t count : kIndexCountBits;
    uint32_t elementSize : kElementSizeBits;
    uint32_t asynchronous : 1;

    HwIndexBuffer() noexcept : count{}, elementSize{}, asynchronous{} {}
    HwIndexBuffer(uint8_t const elementSize, uint32_t const indexCount, bool const async) noexcept
        : count(indexCount), elementSize(elementSize), asynchronous(async) {
        LOG_ASSERT(elementSize > 0 && elementSize <= kMaxElementSize);
        LOG_ASSERT(indexCount < (1u << kIndexCountBits));
    }
};
```

### D2: 位域断言收紧（偏离上游，记录在案）

上游断言为 `indexCount < (1u << 27)`，而 `count` 位宽只有 26——断言允许的取值会被静默截断，断言与布局不自洽。本项目收紧为 `indexCount < (1u << kIndexCountBits)`（即 `< 2^26`）：debug 构建下越界立即暴露，而不是生成一个索引数被截断的缓冲。`elementSize` 保持上游 `(0, 16]` 约束。此偏离需在 tasks 中明确，避免后续对照上游时被当作笔误改回去。

### D3: `GetElementTypeSize` 作为 `Driver` 公共静态成员，定义放独立编译单元

上游为 `Driver::getElementTypeSize`（公共静态）。用户决策：沿用 `Driver` 静态成员落位，理由是该映射属后端通用知识（`ElementType` 定义在公共 `DriverDefine.h`），未来别的后端也要用；同时避免 Vulkan 私有化后其他后端复制一份。

- 命名：按项目规范改 `PascalCase` → `GetElementTypeSize`（`DriverAPI.inc` 之外的新 API 走项目命名，见 D7）
- 返回值：`NODISCARD`，符合"状态查询返回值不应被丢弃"
- 落位：`include/Backend/Driver.h` 声明；**定义放新建 `src/Driver.cpp`**，而非头文件内联——26 分支 `switch` 不适合放进被广泛包含的公共头。`src/Driver.cpp` 由 `file(GLOB_RECURSE src/*.cpp)` 自动收集，构建脚本零改动
- 实现：`switch` + `CASE_FROM_TO`（项目宏规范禁止逐 case 手写两行）。上游用 `sizeof(byte2)`/`sizeof(short3)` 等 Filament math 类型，本项目无这些类型，故以分量宽度字面量展开（`BYTE3`→3、`FLOAT4`→16），语义自明不属魔法数字

### D4: `VulkanIndexBuffer` 复用现有 Proxy 形态，不引入 stagePool

上游构造签名为 `(VulkanContext const&, VmaAllocator, VulkanStagePool&, VulkanBufferCache&, uint8_t, uint32_t)`。本项目 `VulkanBufferProxy` 在上一变更中已按用户决策砍掉 `VulkanStagePool` 参数，故本地签名为 `(const VulkanContextPtr&, VmaAllocator, const VulkanBufferCachePtr&, uint8_t, uint32_t)`。上传路径整体不在范围内，回补 stagePool 只会引入一个无人读取的成员。

`indexType` 推导用 `elementSize == sizeof(uint16_t)` 而非上游的 `elementSize == 2`——语义即"宽度等于 16 位整数"，避免无含义字面量。成员声明顺序为 `indexType`（public）在前、`m_buffer`（private）在后，初始化列表顺序与之一致，规避 `-Wreorder`。

```cpp
VulkanIndexBuffer::VulkanIndexBuffer(const VulkanContextPtr& context, VmaAllocator allocator,
                                     const VulkanBufferCachePtr& bufferCache, uint8_t elementSize, uint32_t indexCount)
    : HwIndexBuffer(elementSize, indexCount, false),
      indexType(elementSize == sizeof(uint16_t) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32),
      m_buffer(context, allocator, bufferCache, VulkanBufferBinding::Index, BufferUsage::STATIC,
               static_cast<uint32_t>(elementSize) * indexCount) {}
```

### D5: 修补句柄引用语义（本次最关键决策）

**缺陷**：`Make` 的实现是「`construct` 后返回 `SharedPtr<D>(obj)`」，而 `Utils::Ref` 初始计数为 0、`SharedPtr(T*)` 构造 `AddRef` 到 1。driver 侧 `auto ib = m_resMgr->Make<...>(...)` 在方法末尾析构，计数 1 → 0 → `OnLastRef` → `destructLaterWithType`，即**对象在创建时就被排入 GC 队列**：

```
现状（缺陷）                                        修正后
──────────────────────────────────────────────     ──────────────────────────────────────────────
CreateIndexBufferR:                                CreateIndexBufferR:
  Make()      → 计数 1（借用视图）                    Make()      → 句柄引用 1 + 借用视图 1 = 2
  方法返回    → 计数 0 → OnLastRef → 入 GC 队列       方法返回    → 计数 1（句柄持有），不入队
DestroyIndexBuffer:                                DestroyIndexBuffer:
  Acquire()   → 计数 1                               Acquire()   → 计数 2
  Destroy()   → 计数 0 → 再次入队                     Destroy()   → Reset 后 1，SubRef 后 0 → 入队一次
Gc(): 同一 id 处理两次                               Gc(): 每个 id 恰好处理一次
```

今天不炸的三个巧合：`ResourceManager::Gc()` 只在 `Terminate()` 里被调用；`HandleAllocator::Deallocate` 允许 `nullptr`（重复入队的第二次退化为 no-op）；`DestroyWithType` 缺分支让多数类型根本没被析构。一旦帧循环开始泵 `gc()`（任何真实驱动的必经之路），第一个巧合消失，对象会在句柄仍存活时被析构。

**修正**（对应上游 `resource_ptr` 的 `inc()`/`dec()` 语义）：

| 上游 `fvkmemory` | 本项目 |
|---|---|
| `resource_ptr::make()` → `inc()` | `Make()` → `construct` + `AddRef()`（句柄引用） |
| driver 显式 `ptr.inc()` | 由 `Make()` 内建，不再要求 driver 手写（少一次漏写机会） |
| `resource_ptr` 析构 → `dec()` | 借用视图 `SharedPtr` 析构 → `SubRef()` |
| `resource_ptr::cast()`（借用） | `Acquire()`（借用视图） |
| `ptr.dec()`（释放句柄引用） | `Destroy()` 内的 `SubRef()` |
| `resource_ptr::construct()`（内部资源） | `AllocateAndConstruct()`（**不**加句柄引用） |

`AllocateAndConstruct` 当前是 `Make<D, D>(AllocHandle<D>(), ...)`，若 `Make` 内建句柄引用，`VulkanBuffer` / `VulkanStageBuffer::Segment` / `VulkanSemaphore` 这三类内部资源会凭空多一份永不释放的引用（`VulkanBufferCache` 归还池、`Segment` 回收、信号量回池全部失效）。故 `AllocateAndConstruct` MUST 改走 `construct<D, D>`，不再经 `Make`——语义上两者本就是"有句柄背书"与"无句柄背书"两种所有权模型。

`Destroy` 尾部加 `LOG_ASSERT(obj->GetRefCount() >= 1)`：命令缓冲仍借用时计数可大于 1（延后销毁是正确行为），小于 1 则说明调用方误对内部资源用了 `Destroy`，立即暴露。

**替代方案（已否决）**：把该修复拆成独立变更，本次只加 IndexBuffer 与类型表分支。否决理由——本次的验收物正是"创建/销毁闭环"，而缺陷使闭环语义为假；分支补齐后一旦有人泵 `gc()`，被析构的恰好是本次新增的对象。修复量约 3 行，且与本次改动同一文件同一函数，拆开反而制造一次"修完类型表再修引用"的中间态。

### D6: 类型表在缓冲族收口（含一处既有 spec 漂移）

`DestroyWithType` 现有 4 个分支（`VulkanBuffer` / `VertexBufferInfo` / `StageSegment` / `Semaphore`），补充后为 7 个。需要同时补 `GetTypeEnum` 特化的只有两个：`VulkanBufferObject`（完全缺失，当前回退 `UndefinedType`）与 `VulkanIndexBuffer`（新增）。`VulkanVertexBuffer` 的特化早已存在、只是分支缺失。

顺带修一处 spec 漂移：`VertexBufferInfo` 分支代码里已存在，但 `vulkan-resource` spec 的「gc 与 terminate 批量销毁」从未列出它——本次 MODIFIED 时补齐记录。

`GetTypeEnum` 特化声明放 `Resource.h`（只需前向声明 `struct VulkanBufferObject;` / `struct VulkanIndexBuffer;`，特化签名只涉及 `ResourceType`），定义放 `Resource.cpp`，与既有 4 个特化同构。

### D7: DriverAPI.inc 缓冲族命名分组

`DriverAPI.inc` 目前混了两套命名：种子方法保持上游小写（`tick` / `createFence`），后加的缓冲族方法用项目 `PascalCase`（`CreateVertexBufferInfo` / `DestroyBufferObject` / `SetVertexBufferObject`）。后者此前未被 spec 覆盖，导致 driver-interface spec 仍写着"方法名 SHALL 保持 Filament 原版拼写"，与实际代码矛盾。

本次不回头改既有方法名（会牵动 `CommandStream` 生成方法与测试），而是把规则显式化：种子集保持上游拼写，缓冲族用 `PascalCase`，并在 spec 中为缓冲族单列一条 requirement。新增的两条按创建/销毁分组插入：

```
DECL_DRIVER_API_TAGGED_R_N(VertexBufferInfoHandle, CreateVertexBufferInfo, ...)
DECL_DRIVER_API_TAGGED_R_N(VertexBufferHandle,     CreateVertexBuffer,     ...)
DECL_DRIVER_API_TAGGED_R_N(BufferObjectHandle,     CreateBufferObject,     ...)
DECL_DRIVER_API_TAGGED_R_N(IndexBufferHandle,      CreateIndexBuffer,      ElementType, elementType, uint32_t, indexCount, BufferUsage, usage)

DECL_DRIVER_API_N(DestroyBufferObject, BufferObjectHandle, boh)
DECL_DRIVER_API_N(DestroyIndexBuffer,  IndexBufferHandle,  ibh)

DECL_DRIVER_API_N(SetVertexBufferObject, VertexBufferHandle, vbh, uint32_t, index, BufferObjectHandle, boh)
```

`CommandStream` 与 `ConcreteDispatcher` 均由 `DriverAPI.inc` 宏展开自动生成记录方法与派发表，无需手写任何胶水代码——这也是把新方法放进 inc 而非在 `VulkanDriver` 自造接口的原因。

### D8: 验证策略

`tests/Engine.cpp` 已能构造真实 Vulkan driver（`PlatformFactory::Create` + `CreateDriver`）并在独立驱动线程上跑 `CommandStream`，`bin/BackendTests` 已可执行，因此本次验证不止于构建：

- `Engine` 增 `CreateIndexBuffer` / `DestroyIndexBuffer` 转发到 `m_stream`（与既有 `CreateFence` 同模式），走完整命令流路径
- `App::Run` 的 8 帧循环内做创建-销毁往返（16 位与 32 位索引各一次），覆盖 `GetElementTypeSize` 的两条分支
- 运行 `BackendTests`，以"无 `LOG_CRITICAL` 输出 + 进程正常退出"为通过判据；`Terminate()` 会清空 GC 队列，因此销毁分支与池块归还在该路径上真实执行

无法被测试覆盖的部分（帧循环未泵 `gc()`，无法在运行期观察"销毁才入队"）：以代码走查 + 与上游 `resource_ptr` 语义对照为准，在 tasks 中单列一条复核项。

运行该测试需要 Vulkan 运行时环境变量（`.vscode/launch.json` 只设了其一）：

```sh
DYLD_LIBRARY_PATH=/opt/homebrew/lib \
VK_ICD_FILENAMES=/Users/turiing/VulkanSDK/1.4.313.1/macOS/share/vulkan/icd.d/MoltenVK_icd.json \
./bin/BackendTests
```

### D9: `VulkanDriver::Create` 补 `handleArenaSize` 兜底（实施中新增，用户决策）

首次运行测试时驱动构造期即崩在 `FreeList.cpp:15`（`Assertion failed: (p >= begin && p < end)`）。根因：`DriverConfig::handleArenaSize` 默认值为 0，上游 `VulkanDriver::create()` 会用 `std::max(driverConfig.handleArenaSize, FVK_HANDLE_ARENA_SIZE_IN_MB * 1MB)` 兜底（`FVK_HANDLE_ARENA_SIZE_IN_MB = 8`），本项目 `Create()` 直接把 0 透传给 `ResourceManager` → `HandleAllocator` 以 0 字节建 arena。

修复：文件内 `constexpr size_t kMinHandleArenaSize = 8u * 1024u * 1024u;` + `validConfig.handleArenaSize = std::max(config.handleArenaSize, kMinHandleArenaSize)`。常量放 `VulkanDriver.cpp` 匿名命名空间（仅此一处使用，不上浮到 `VkDef.h`）。

### D10: `DestroyResources` 终止顺序与幂等（实施中新增，用户决策）

D9 修完后测试推进到终止期，又暴露 `DestroyResources` 的两个问题（均属既有缺陷，`port-vulkan-buffer` 的 D7 已记录生命周期约束但未实现）：

1. **池内 VkBuffer 未归还 VMA**：原实现 `m_resMgr->Terminate()` 之后直接 `m_bufferCache.Reset()`，跳过了 `VulkanBufferCache::Terminate()`。GC 销毁 `VulkanIndexBuffer` → `~VulkanBufferProxy` → `~VulkanBuffer` → `OnRecycle` 把 gpuBuffer 放回池（裸指针，无 RAII），随后 `Reset()` 丢掉 cache → 这些 `VkBuffer` 永不 `vmaDestroyBuffer` → `vmaDestroyAllocator` 触发 `Some allocations were not freed` 断言。改为 `Terminate()` 后再 `Reset()`，`m_stagePool` 同理。
2. **不可重入**：`terminate()` 与 `~VulkanDriver()` 都会调用 `DestroyResources()`。原实现两步都是"Reset 空指针无害"，第二次调用安全；改为 `->Terminate()` 后第二次调用会在已 `Reset()` 的空 `SharedPtr` 上解引用（实测 `EXC_BAD_ACCESS ... __tree::begin(this=0x28)`）。故每步以 `if (m_bufferCache)` / `if (m_stagePool)` 判空后成对执行，使该函数幂等。

终止顺序固化为：`ResourceManager::Terminate()`（清空 GC 队列，全部资源引用归零并归还池）→ 各池 `Terminate()`（归还 VMA）→ 各池 `Reset()` → `vmaDestroyAllocator`。

## Risks / Trade-offs

- [`Make` 语义变更影响既有 3 个 driver 调用点] → 这 3 处（`CreateVertexBufferInfoR` / `CreateVertexBufferR` / `CreateBufferObjectR`）正是句柄背书资源，修正后行为才正确；`AllocateAndConstruct` 的 3 个使用方（`VulkanBufferCache` / `VulkanStagePool` / `VulkanSemaphoreManager`）计数语义不变（仍为 1）
- [`Destroy` 被误用于内部资源会导致计数下溢] → `Destroy` 尾部 `LOG_ASSERT(GetRefCount() >= 1)` 前置校验；当前 `Destroy` 仅由 driver 对 `Make` 创建的对象调用
- [补上 `VertexBuffer` / `BufferObject` 销毁分支后，若将来直接泵 `gc()` 而未修引用语义会导致 use-after-free] → 引用语义与分支在同一变更内一起修，二者不可分离（D5）
- [位域断言比上游更严（`2^26` vs 上游 `2^27`）] → 上游断言与位宽不自洽；收紧为真实容量，越界在 debug 立即暴露而非静默截断（D2）
- [`GetElementTypeSize` 用分量宽度字面量而非 `sizeof` 复合类型] → 项目无 `byte2`/`short3` 等类型；`CASE_FROM_TO` 展开后语义自明，且新增 ElementType 时遗漏会被 `return 0` 兜底场景测出
- [GC 仍未在帧循环泵，闭环的"帧末"环节未被运行期覆盖] → 属 `VulkanCommands` 接线 / flush-finish 变更的范围；本次在 `Terminate()` 路径上验证销毁分支可达
- [测试往返每帧创建索引缓冲会累积 GC 队列至 `Terminate()`] → 队列仅存 `{type, id}` 对，8 帧 16 条约数百字节，可接受；不引入内存增长风险

## Open Questions

- `.vscode/launch.json` 只设了 `DYLD_LIBRARY_PATH`，缺 `VK_ICD_FILENAMES`，在 IDE 里直接调试 `BackendTests` 仍会停在 `volkInitialize()` 失败上；是否把它补进 launch 配置（或改为在 CMake 的 test 配置里设环境）
- 帧循环何时泵 `ResourceManager::Gc()`：属 `VulkanCommands` 接线/`flush` 变更，需与 `VulkanBufferCache::Gc()` / `VulkanStagePool::Gc()` 的调用时机一并设计
- `updateIndexBuffer` 及其依赖链（`BufferDescriptor`、`Driver::scheduleDestroy`、`VulkanBufferProxy::LoadFromCpu`、`BufferUsage` 位运算符、`VulkanStagePool` 回补）作为独立变更
- `VulkanHandle.h` 层其余类型（Texture / RenderTarget / Program / RenderPrimitive 等）仍未被任何 spec 覆盖，存在 spec 漂移——是否单开一个"补齐 VulkanHandles 类型表"的变更
- 索引绘制路径（`VulkanRenderPrimitive` + `vkCmdBindIndexBuffer`）依赖渲染通道/管线，暂不排期
