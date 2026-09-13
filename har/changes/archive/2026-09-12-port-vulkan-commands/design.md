# Design: port-vulkan-commands

## Context

上游 `VulkanCommands.h/.cpp` 装的是四层解耦的职责，但四者共享同一套支撑设施，无法只取其一：

```
VulkanCommands           门面：单池（命令录制/提交）、依赖注入、最近提交围栏
  └ VulkanCommandBufferPool   45 槽位池 + 提交位图 + 内嵌 VulkanFencePool
      └ VulkanCommandBuffer   单缓冲：录制 / 标记 / 提交 / 借用资源
          ├ VulkanFencePool          → VulkanCmdFence（shared_ptr 状态包装）
          └ VulkanSemaphoreManager   → VulkanSemaphore（Resource 包装）
```

其中只有 `VulkanCommandBuffer::Acquire` 是本次的**能力增量**：它是本项目第一个把 `Resource` 的 `SharedPtr` 跨帧保活的场所——`port-vulkan-resource` 建立的"引用归零 → GC 队列 → `DestroyWithType`"链路此前从未被真实走到（`VulkanBuffer`/`Segment` 都是即取即还）。其余三个类都是这条链路的承载结构。

本项目现状：`vulkan-resource` 生命周期层就绪、`VulkanContext` 提供 `IsDebugUtilsSupported()`/`IsDebugMarkersSupported()`/`GetFenceExportFlags()`/`IsProtectedMemorySupported()`、`VulQueue` 支持受保护队列且 `VulkanPlatformPrivate` 已持有图形与受保护队列、`VK_UTILS` 命名空间已存在（`VkUtils.h`）。缺口集中在两处：支撑设施（围栏池 / 信号量池）不存在，以及 `VulkanPlatform` 未公开队列。

## Goals / Non-Goals

**Goals:**

- 完整移植命令录制层的四个类，算法与上游保持一致（45 槽位轮转、双池提交顺序、依赖信号链、标记栈跨缓冲重建）
- 一并落地其依赖闭包：`VulkanCmdFence`、`VulkanFencePool`、`VulkanSemaphore` + `VulkanSemaphoreManager`
- 让 `VulkanSemaphore` 成为类型表第三个真实资源，把"资源析构回调归还外部池"这条模式（`VulkanBuffer`/`Segment` 已有）推广到非句柄池对象
- 保留调试组标记，与上游同构；受保护命令路径按 D14 去掉
- 全量构建 + tests 构建通过，新增文件无警告

**Non-Goals:**

- 不接线 `VulkanDriver`：`tick`/`flush`/`finish` 仍为空壳，`VulkanCommands` 等对象在本仓库内无构造方
- 不移植 `VulkanAsyncHandles.h` 的 `VulkanProgram`/`VulkanFence`/`VulkanSync`/`VulkanTimerQuery`（`vulkan-sync` 只取 `VulkanCmdFence`）
- 不移植 `FVK_SYSTRACE_*` 系列
- 不移植 swapchain、query manager、blitter 等命令层的下游消费者
- 不补 `DriverConfig::handleArenaSize` 默认值问题与 tests 的 `DYLD_LIBRARY_PATH` 问题（既有缺口，属独立变更）
- 不写单元测试：无调用方、且 tests 工程当前无法构造驱动（见 Risks）

## Decisions

### D1: 变更范围——依赖闭包整体搬

四个类无法拆开落地：`VulkanCommandBuffer` 的构造函数就要 `VulkanFencePool&` 与 `VulkanSemaphoreManager&`，`CommandBufferPool` 内嵌 `VulkanFencePool` 值成员。上游把它们放在同一目录正是这个原因。

代价：单次变更 16 个新增文件（8 个文件对），粒度偏大。收益：不存在"半条链"的中间态（若先落地围栏池而不落地命令缓冲，围栏池连一个 `AcquireFenceStatus` 调用方都没有，验证价值为零）。

考虑过的替代方案：拆成 `port-vulkan-sync` + `port-vulkan-commands` 两个变更——已评估并否决，理由同上；两个变更的验证手段完全相同（都只有构建），拆开只增加一次文档成本。

### D2: 文件落位与命名

```
src/vulkan/sync/
├── VulkanCmdFence.h/.cpp          // VkFence 状态包装（shared_ptr 共享）
├── VulkanFencePool.h/.cpp         // VkFence 池化与淘汰
├── VulkanSemaphore.h/.cpp         // Resource 包装
└── VulkanSemaphoreManager.h/.cpp  // VkSemaphore 池化

src/vulkan/commands/
├── VulkanGroupMarkers.h/.cpp      // 调试标记栈（仅 BVK_DEBUG_GROUP_MARKERS 下编译）
├── VulkanCommandBuffer.h/.cpp     // 单个命令缓冲
├── VulkanCommandBufferPool.h/.cpp // 槽位池
└── VulkanCommands.h/.cpp          // 门面：单池
```

与既有 `buffer/`、`core/`、`platform/`、`resource/`、`stage/` 分域方式一致。上游把四类同置一文件，本次按职责拆到两个域：`sync/` 装"可被 swapchain/query 复用的同步原语"，`commands/` 装录制层。

**一类一文件**（用户决策，覆盖初稿的"四类同置 `VulkanCommands.h`"）：`commands/` 下四个类各自拥有同名头/源文件，`VulkanCommands.h` 只保留门面类。理由是依赖方向本就单向（门面 → 池 → 缓冲 → sync），同置一文件会让任何一个消费者的 include 面被放大到四份实现细节；拆开后 `VulkanCommandBuffer.h` 不必看见门面，`VulkanGroupMarkers.h` 也只被池与门面按需拉入。代价是 `VulkanCommands.h` 从"上游一一对应"变成项目自有布局，后续与上游比对时需按类名而非文件名定位。

命名：`CommandBufferPool` → `VulkanCommandBufferPool`（项目内无裸 `CommandBufferPool` 语义，且易与 `command/` 目录下的命令流 `CommandBufferQueue` 混淆）；其余类名与上游一致。

### D3: 资源借用语义——变体塌陷为单一 `SharedPtr<Resource>`

上游：

```cpp
using HeldResource = std::variant<resource_ptr<Resource>, resource_ptr<ThreadSafeResource>>;
std::vector<HeldResource> mResources;

template <typename T, typename = enable_if_t<is_base_of_v<Resource,T> || is_base_of_v<ThreadSafeResource,T>>>
inline void acquire(resource_ptr<T> resource) { mResources.push_back(resource); }
```

本项目没有 `ThreadSafeResource` 分裂——`port-vulkan-resource` 已明确"砍掉 `ThreadSafeResource` 与 `isThreadSafeType` 分裂（`Ref` 计数本身原子）"。故变体塌缩为 `std::vector<NS_UTILS::SharedPtr<Resource>>`。

**坑点**：`Utils::SharedPtr`（`3rd/Utils/include/Utils/mem/SharedPtr.h`）**只有** `explicit SharedPtr(T*)`，没有 `SharedPtr<Derived>` → `SharedPtr<Base>` 的转换构造。因此 `Acquire` 实现为：

```cpp
template <typename T, typename = std::enable_if_t<std::is_base_of_v<Resource, T>>>
inline void Acquire(NS_UTILS::SharedPtr<T> resource) {
    m_resources.emplace_back(resource.Get());
}
```

`emplace_back(Resource*)` 走 `explicit SharedPtr(T*)` 直接初始化，对 `T = Resource` 同样成立。代价是一次多余的 AddRef/SubRef（形参拷贝 → 重建），可接受。

考虑过的替代方案：给 `Utils::SharedPtr` 补一个转换构造函数——否决，改公共工具库影响面外溢到全部既有使用者，而收益只是省一对原子操作。

### D4: 传参形态改为 `Ptr`

按项目智能指针约定（`cpp.md`「已声明 Ptr 别名的类型，函数参数一律用 `const Ptr &`，成员也必须存 `Ptr`」），上游的 `VulkanContext const& mContext` 与 `VulkanSemaphoreManager* m_semaphoreManager` 分别改为 `VulkanContextPtr` 与 `const VulkanSemaphoreManagerPtr&`（成员存 Ptr）。这与 `VulkanBufferCache`/`VulkanStagePool` 的既有形态一致。

`VulkanFencePool` 作为 `VulkanCommandBufferPool` 的**值成员**保留（上游即如此，且其生命周期天然被池包住），不作为 `Ptr` 传递。

### D5: `utils::CString` → `NS_UTILS::String`

`VulkanGroupMarkers` 与 `VulkanCommandBufferPool::TopMarker()` 用到 `CString`。本项目 `Utils/string` 下四个候选中：

| 候选 | 是否适用 | 原因 |
|---|---|---|
| `String` | ✅ | 堆分配、NUL 结尾、可拷贝可移动、`c_str()`/`size()`；`String(StringLiteral<N>)` 为隐式构造，`return "";` 原样可用 |
| `FixedString<N>` | ❌ | 定长数组，需编译期尺寸，且无长度信息 |
| `ImmutableString` | ❌ | handle tag 专用，`static_assert(sizeof <= 16)`，语义不符 |
| `StaticString` | ❌ | 仅引用字面量，无法承载运行期拼接的标记 |

映射：`CString{marker}` → `NS_UTILS::String{marker}`（`explicit String(const char*)`，直接初始化合法）。

### D6: `utils::bitset64` → `std::bitset<kMaxCommandBuffers>`，并改写尺寸断言

上游 `CommandBufferPool::ActiveBuffers = utils::bitset64`，并断言 `sizeof(ActiveBuffers) * 8 >= CAPACITY`。本项目无 `bitset64`，改用 `std::bitset<kMaxCommandBuffers>`（45 位，libc++ 下 `sizeof` 为 8）。

**该尺寸断言必须改写而非照抄**：`std::bitset<N>` 不保证存储布局，`sizeof(bitset<45>)*8 >= 45` 在 libc++ 成立但属实现细节。保留有意义的另一条：

```cpp
static_assert(kMaxCommandBuffers < 128);   // int8_t 索引上限
```

上游 `using BufferList = utils::FixedCapacityVector<std::unique_ptr<VulkanCommandBuffer>>;` 声明了但 `mBuffers` 实际是 `std::vector`——死代码，直接不移植。

### D7: 常量落位——共享的进 `VkDef.h`，文件私有的留在 `.cpp`

| 上游 | 本项目 | 理由 |
|---|---|---|
| `VKALLOC` | `kVkAlloc`（`src/vulkan/VkDef.h`） | 四个新文件全部使用，属跨文件共享定义 |
| `FVK_MAX_COMMAND_BUFFERS` (3×15) | `kMaxCommandBuffers`（同上） | 命令缓冲池、围栏池容量、信号量池初始容量三处使用 |
| `TIME_BEFORE_EVICTION` (3) | `kFenceTimeBeforeEviction`（`VulkanFencePool.cpp` 匿名命名空间） | 仅该文件使用，按"仅适用于本功能的定义留在功能范围内"就地定义 |

（注：`VulkanStagePool` 的先例同样把 `kTimeBeforeEviction` 放在 `.cpp` 匿名命名空间。两者语义不同但都是文件私有，无需合并。）

### D8: `FenceStatus` 落位 `DriverDefine.h`，命名取 PascalCase

`VulkanCmdFence::Wait()` 的返回类型在上游是 `backend/DriverEnums.h` 的 `enum class FenceStatus : int8_t { ERROR = -1, CONDITION_SATISFIED = 0, TIMEOUT_EXPIRED = 1 }`，本项目没有。落位 `include/Backend/DriverDefine.h`（driver 通用定义层，将来 `Driver::waitFence` 语义也要用它）。

命名冲突：项目 `cpp.md` 要求枚举值 PascalCase（`ResourceType` 亦然），但 `DriverDefine.h` 现有枚举（`BackendType::VULKAN`、`ElementType::FLOAT`）是 UPPER_SNAKE。本设计取 **PascalCase**（`Error`/`ConditionSatisfied`/`TimeoutExpired`）——规则文件优先，且 `ResourceType` 提供了同层先例。若复核时判定 `DriverDefine.h` 局部一致性更重要，改动成本仅为 4 处引用。

上游的 `FENCE_WAIT_FOR_EVER` 无调用方，不移植。

### D9: `VulkanCmdFence` 完整移植（含 `Wait()`/`Cancel()`）

上一轮探索曾建议砍掉 `Wait()`/`Cancel()`（无调用方、需自造 `FenceStatus`），用户选择完整移植。代价已确认：新增 `FenceStatus` 通用定义（D8）、保留 `std::condition_variable_any` + `mCanceled` + `std::shared_mutex` 的完整等待语义。收益：将来接线 `Driver::waitFence` 时无需回头改这个类，且 `Terminate()` 的"取消在途等待"路径得以保留。

### D10: 平台队列访问器本次添加且接受无调用方

`VulkanCommands` 构造需要设备、队列句柄与队列族索引，这 3 个值目前拿不到——`VulkanPlatform` 只公开了 `GetVkInstance()`/`GetVkPhysicalDevice()`/`GetVkDevice()`，而队列信息躺在 `VulkanPlatformPrivate` 里。

本次按上游 `VulkanPlatform` 的 `getGraphicsQueue*` 系列补 3 个 PascalCase 访问器（受保护队列的 3 个随 D14 一并去掉）。**它们本次不会有调用方**（因为不接线 driver），属为接线变更预置；接受这一点的理由是"只把已有值暴露出来"风险极低，且避免接线时再改公共头 `include/Backend/platform/`。

`GetVkGraphicsQueue()` 在池未创建时返回 `VK_NULL_HANDLE`。

### D11: 生命周期与所有权约束

三条硬约束，均来自"析构路径回调外部对象"这一模式：

| 对象 | 约束 | 原因 |
|---|---|---|
| `VulkanSemaphoreManager` | SHALL 比其产出的全部 `VulkanSemaphore` 活得久 | `~VulkanSemaphore` 回调 `Recycle` |
| `VulkanFencePool` | SHALL 比引用它的 `VulkanCommandBuffer` 活得久 | 后者持 `VulkanFencePool&` |
| `VulkanCommands`/`VulkanSemaphoreManager` | SHALL 在 `VkDevice` 仍存活时 `Terminate()` | 销毁 `VkCommandPool`/`VkFence`/`VkSemaphore` |

driver 接线时应照抄上游形态（`VulkanDriver` 以**值成员**持有 `VulkanSemaphoreManager` 与 `VulkanCommands`，声明顺序即析构逆序），使约束由 C++ 对象生命周期自动满足。本次不接线，故这些约束写入 spec 作为契约。

`VulkanCommandBufferPool` 析构顺序（上游）：`Wait()` → `Gc()` → 销毁 `VkCommandPool` → `VulkanFencePool::Terminate()`。命令缓冲的围栏必须先终止，否则已归还句柄会写回已销毁的池。

### D12: 不引入 `StaticVector`，等待数组用 `std::array` + 计数

上游用 `fvkutils::StaticVector<T, 2>` 装 `VulkanCommandBuffer` 的两组等待数组，是因为该类在 Filament 内部被多处复用（`VulkanProgram::BindingList = StaticVector<uint16_t, MAX_SAMPLER_COUNT>` 等）。但在本项目的移植范围内它**只有一个使用点**（`VulkanCommandBuffer` 的 `m_waitSemaphores`/`m_waitSemaphoreStages`），且只用到 `PushBack`/`Clear`/`Size`/`Data` 四个方法——`PopBack`/`Back`/`Find`/`operator==`/迭代器全部闲置。为一个使用点引入一个自研容器头（外加 `src/vulkan/utils/` 目录）不成比例，改用标准库：

```cpp
// 一次提交最多累积两条等待：注入的外部依赖 + 上一提交的完成信号量
static constexpr uint32_t kMaxWaitSemaphores = 2;

std::array<VkSemaphore, kMaxWaitSemaphores>          m_waitSemaphores{};
std::array<VkPipelineStageFlags, kMaxWaitSemaphores> m_waitSemaphoreStages{};
uint32_t                                             m_waitSemaphoreCount = 0;
```

`InsertWait` 是唯一写入口，在写入前 `LOG_ASSERT(m_waitSemaphoreCount < kMaxWaitSemaphores)` 并把两个数组与计数一起推进；`Submit` 以计数作为 `waitSemaphoreCount`，`std::array::data()` 在计数为 0 时仍返回有效指针，`VkSubmitInfo` 语义不受影响。

收益：少一个自研容器与一个目录；行为面完全等价（`StaticVector` 对 trivial 类型的 `Clear()`/`pop_back()` 不做事，`data()`/`size()` 与数组+计数同义）。代价：计数与数组必须同步——已由"唯一写入口"约束住。

顺带消掉的问题：上游 `StaticVector::Back()` 是 `*(begin() + mSize)`（越界一位，应为 `mSize - 1`）。该 bug 在上游被使用模式掩盖（只 push/clear/data，从不 `Back()`）；不移植该类后此问题自然不存在。

替代方案评估：`std::vector<T>` 带 `reserve(2)`——否决，为一个定长 2 的数组引入堆分配与 `begin/end` 迭代器失效语义；`std::inplace_vector`（C++26）——本项目标准为 C++20，不可用；`Utils` 子模块——确认无 `FixedCapacityVector` 等同类容器（只有 `CircularBuffer`）。

### D13: 验证策略——构建 + 人工复核

不接线 driver ⇒ 16 个新增文件在本仓库内零运行期调用方。验证手段：

1. 全量构建 `Backend` 与 `BackendTests`，新增文件无警告
2. clang-format 检查（include 顺序按项目既有约定保留）
3. 范围边界 grep：无 `FVK_` 前缀残留、无 `VulkanProgram`/`VulkanSync`/`VulkanTimerQuery`、无 `bluevk`
4. spec 中的运行期 Scenario 作为契约复核清单，实际验证推迟到 driver 接线变更

这与 `port-vulkan-buffer`/`port-vulkan-stage-buffer` 的口径一致（两者同样无调用方落地）。

### D14: 去掉受保护（protected）命令路径

初稿按上游同构移植了普通 + 受保护双池，用户复核后决定整体去掉。

**依据（实测）**：在本机 Apple M4 / MoltenVK 1.4.1 上查询物理设备特性，`protectedMemory = FALSE`，且 4 个队列族中带 `VK_QUEUE_PROTECTED_BIT` 的为 **0 个**。链路上游因此是断的：`VulkanContext::IsProtectedMemorySupported()` 为假 → `VulkanPlatform` 不请求受保护队列 → `GetVkProtectedGraphicsQueue()` 恒返回 `VK_NULL_HANDLE` → 任何一次 `GetProtected()` 都会撞上它开头的 `LOG_ASSERT(m_protectedQueue != VK_NULL_HANDLE)`。也就是说这条分支不是"暂时跑不到"，而是**在 macOS 上永远跑不到**，只在 Android / Windows 的 DRM 播放设备上才可能被验证。

**去掉的范围**：

| 位置 | 处置 |
|---|---|
| `VulkanCommandBuffer` | 去掉 `isProtected` 形参与 `m_isProtected` 成员；`Submit()` 不再链 `VkProtectedSubmitInfo` |
| `VulkanCommandBufferPool` | 去掉 `isProtected` 形参；命令池 flags 不再追加 `VK_COMMAND_POOL_CREATE_PROTECTED_BIT` |
| `VulkanCommands` | 去掉 `protectedQueue`/`protectedQueueFamilyIndex` 形参、`m_protectedPool`、`GetProtected()`；`Flush`/`Wait`/`Gc`/`UpdateFences`/`Terminate` 与四个调试标记接口全部退化为单池 |
| `VulkanPlatform` | 去掉本次为它新增的 3 个受保护队列访问器（只保留图形队列的 3 个） |
| `VulLogicDevice` / `VulQueue` / `VulkanContext` 的受保护内存支持 | **保留不动**——那是平台层既有（已提交）的能力，不为命令层而存在；将来真接 DRM 时，命令层的这条路径可参照上游 `VulkanCommands.cpp` 加回 |

**代价**：将来要支持 DRM 受保护内容，需要把命令层的双池结构重新加回（平台层设施还在，加回量约 60 行 + 一次 `Flush` 双池遍历）。这是有意接受的不对称：现在保留一条**永不可达**的分支，成本是每次读 `Flush`/`Terminate` 都要在脑子里过一遍双池语义，而收益为零。

**一个残留**：`VulkanPlatformPrivate::m_pProtectedGraphicsQueue` 仍会在支持受保护内存的设备上被创建，但已无访问者。本轮不动它（属平台层既有代码，且在不支持的设备上本就是空操作）。

## Risks / Trade-offs

- [无运行期验证：`VulkanSemaphore` 的 Resource 销毁链、围栏池回收、45 槽位轮转全部未经执行] → D13 明确记入 Non-Goals；把"运行期验证"列为 driver 接线变更的验收项。构件层面的风险由 spec Scenario 清单人工复核兜住
- [悬垂：池先于其产出对象析构 → 析构回调写入已析构对象] → D11 三条约束；spec 的「同步设施的线程与生命周期约束」需求以 Scenario 形式记录违约后果
- [改写上游断言：`sizeof(ActiveBuffers)*8 >= CAPACITY` 对 `std::bitset` 不成立] → D6 换成 `kMaxCommandBuffers < 128`（`int8_t` 索引的真实约束），并在 tasks 中记录偏离
- [`std::bitset<45>` 的实现依赖性：若将来换 STL 或容量显著增长，位图尺寸假设可能失效] → 位图只用于 `forEachSetBit` 式的遍历与位运算，不依赖尺寸；容量上限由 `kMaxCommandBuffers` 与 `static_assert(< 128)` 双重约束
- [等待信号量数组容量 2：超出仅在 debug 断言拦截] → 与上游一致（release 下越界写入）；`InsertWait` 是唯一写入口且每次提交后计数归零，实际使用模式不会超出
- [FenceStatus 命名与 `DriverDefine.h` 局部风格不一致] → D8 已记录替代方案；改动成本 4 处引用，复核阶段可低成本翻转
- [`VulkanCmdFence` 的 `Wait()` 引入 `std::condition_variable_any` 与 `shared_mutex`，而本项目其余同步用 `std::mutex`] → 属上游语义（等待期间需释放共享锁），保留；不引入新依赖
- [平台访问器无调用方 → 编译通过但语义未被检验] → D10；其实现只是转发 `VulkanPlatformPrivate` 的既有字段，逻辑面极小

## Open Questions

- **`ResourceType` 枚举值命名口径**：归档 `port-vulkan-*` 时发现主 spec 写着 UPPER_SNAKE（`UNDEFINED_TYPE`）而代码是 PascalCase（`UndefinedType`）。本变更的 `vulkan-resource` delta 沿用主 spec 现有措辞以保持一致，但该矛盾需一次 `/har-doc` 或 `/har-update` 统一——是改措辞还是改代码，待定
- **资源层方法名同批口径**：同一次统一应覆盖 `vulkan-resource` 主 spec 中的 `gc()`/`terminate()`/`print()` 与代码 `Gc()`/`Terminate()`/`Print()` 的差异（本变更 delta 沿用主 spec 措辞，未单方面改写）
- `FENCE_WAIT_FOR_EVER` 是否随 `FenceStatus` 一并补：本次不补（无调用方）；将来接 `Driver::waitFence` 时若需要再加
- driver 接线时 `VulkanCommands` 与 `VulkanSemaphoreManager` 在 `VulkanDriver` 中的成员声明位置（须满足 D11 的析构顺序），以及 `VulkanPlatform` 队列访问器是否直接透传给 `VulkanCommands` 构造
- `VulkanDriver::pushGroupMarker` 等调试标记入口是否随接线一并补：本次落地了 `VulkanGroupMarkers` 与池侧接口，但驱动侧入口不在范围
- 是否把 `VulkanCmdFence` 的 `FenceStatus` 与将来 `Driver` 的 `waitFence` 返回类型统一（当前只是同一个枚举）
