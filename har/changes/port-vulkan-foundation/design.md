# Design: port-vulkan-foundation

## Context

本变更是后续 6 个变更的公共前置，自身不产出任何 Vulkan 渲染能力。它的存在理由是把「上游 17 个组件共享的底层缺口」一次性收口，使后续每个组件的变更只需专注移植本身。

现状盘点：

| 缺口 | 上游引用密度 | 项目现状 |
|---|---|---|
| `utils/Panic.h` | 24 处 include | 无；已有 `LOG_ASSERT` / `LOG_CRITICAL` |
| `utils/Hash.h` | 7 处（`hash::MurmurHashFn<Key>`） | 无 |
| `utils/bitset.h` | 7 处 | 无；既有代码用 `std::bitset` |
| `utils/debug.h` | 6 处（`assert_invariant`） | 无；既有 `LOG_ASSERT` 语义相近但 release 下同样失效 |
| `utils/FixedCapacityVector.h` | 12 处 | 无；既有代码用 `std::vector` |
| `utils/Mutex.h` | 5 处 | 无；既有代码用 `std::mutex` |
| `utils/CString.h` | 4 处 | `NS_UTILS::String` / `ImmutableString` |
| `utils/RangeMap.h` + `utils/Range.h` | 1 处（连带 `Range<T>`） | 无 |
| `CallbackHandler` | `VulkanSync` / 全部 async 方法签名 | 无 |
| `CallbackManager` | `VulkanPipelineCache` | 无 |
| `CompilerThreadPool` | `VulkanPipelineCache` | 无 |
| `DriverBase` | `VulkanDriver` 基类 | 无 |
| `ThreadSafeResource` | 4 个 Hw 类型 | 无（`ResourceManager` 已简化掉） |

关键既有事实（探索阶段确认，决定了本变更的成本下限）：

- `NS_UTILS::Ref` 已使用 `std::atomic<int32_t> m_refCount`（`3rd/Utils/include/Utils/mem/Ref.h:49`），并提供 `OnLastRef()` 虚回调——`Resource` 已借此实现"归零不析构、入 GC 队列"。**线程安全的引用计数已经存在**，`ThreadSafeResource` 不需要自带计数器。
- `src/vulkan/` 全目录**零引用** `JobQueue` / `WorkStealingJobQueue`（探索阶段逐文件 grep 确认）。上一轮探索中把 `JobQueue` 列为前置缺口是误判。
- `VulkanPipelineCache.cpp` 对 `utils::JobSystem` 的全部使用只有 `setThreadName` / `setThreadPriority` 两个调用点（第 86、88 行）。
- `tsl::robin_map` 出现在 7 个文件（`VulkanFboCache.h` / `VulkanPipelineCache.h` / `VulkanPipelineCache.cpp` / `VulkanDescriptorSetLayoutCache.h` / `VulkanDescriptorSetCache.h` / `VulkanSamplerCache.h` / `VulkanYcbcrConversionCache.h`），均尚未移植。

## Goals / Non-Goals

**Goals:**

- 补齐 `hash::MurmurHashFn` / `Bitset<T, N>` / 断言宏 / `Range<T>` / `RangeMap` 六件通用设施
- 建立并可复用的 `CallbackHandler` + `CallbackManager` + `CompilerThreadPool` 异步底座
- `ThreadSafeResource` 回补，使 `VulkanProgram` / `VulkanFence` / `VulkanSync` / `VulkanTimerQuery` 能按上游语义落地
- 把 `robin_map` → `std`、`Invocable` → `std::function` 等 9 条映射固化为书面约定，后续变更直接引用不再各自决策
- 本变更可独立构建、独立跑通 `bin/BackendTests`（不引入任何行为变化，纯增量）

**Non-Goals:**

- 不移植任何 Vulkan 组件（`VulkanTexture` / `VulkanPipelineCache` 等全部留给后续变更）
- 不移植 `JobQueue` / `WorkStealingJobQueue`（Vulkan 后端不需要）
- 不实现 `FeatureFlagManager` 的完整特性开关体系，只保留 `VulkanDriver` 构造期需要的最小形态
- 不移植 `utils::io::ostream`（上游 `ostream.cpp` 550 行，本项目用 spdlog）
- 不改 `Utils::Ref` / `HandleAllocator` / `SharedPtr`
- 不为 `ResourceType` 中尚未有实现的枚举补 `GetTypeEnum` 特化（留给引入对应组件的变更）

## Decisions

### D1: 通用设施放 `3rd/Utils` 子模块；新引入的通用容器一律用项目命名

**落位**：新增 6 个头文件——`Hash.h` / `Bitset.h` / `Panic.h` / `Debug.h` / `Range.h` / `RangeMap.h`——与后端领域无关，属项目统一定义通用代码层。项目已有先例：`CASE_FROM_TO`、`NODISCARD`、`Soa`、`SharedPtr` 均在 `3rd/Utils` 内。

**代价**：改动落在子模块仓库，须在 `3rd/Utils` 内单独提交，本项目仓库只记录子模块指针更新。这符合 `.dsh/rules/git.md`「移植按主题拆分」与 `.dsh/rules/code-style.md`「第三方库一律使用 git submodule」的组合要求。

**已否决的替代方案**：放本项目 `src/` 下新建 `src/utils/`。否决理由——`Hash` / `Bitset` 会被后端各层引用，放 `src/` 会让 `include/Backend/` 下的公共头反向依赖 `src/`，违反既有分层。

**命名（用户决策，实施阶段确立）**：类型与公有方法一律用项目 `PascalCase`，不沿用上游小写。即：

| 上游 | 本项目 |
|---|---|
| `utils::bitset32` / `bitset64` | `NS_UTILS::Bitset32` / `Bitset64` |
| `bitset::forEachSetBit` / `set` / `unset` / `test` / `count` | `Bitset::ForEachSetBit` / `Set` / `Unset` / `Test` / `Count` |
| `utils::RangeMap::add` / `get` / `clear` / `rangeCount` | `RangeMap::Add` / `Get` / `Clear` / `RangeCount` |
| `utils::Range` 的公开成员 `first` / `last` | `Range` 的私有 `m_first` / `m_last` + `First()` / `Last()` |
| `utils::hash::murmur3` / `combine` | `NS_UTILS::hash::Murmur3` / `Combine` |
| `utils::hash::MurmurHashFn<T>` | 名称已合规，直接沿用 |

**与「保上游拼写」策略的边界**：变更 2 的 `math::` 类型与枚举值名保留上游拼写，理由是「上游调用点零改写」。本次选择相反——因为 `Bitset` / `RangeMap` 的上游调用点**全部位于尚未移植的文件**（变更 4–7 才引入），改名成本在这一侧为零；而 `math::` 类型出现在**变更 2 已定稿的 `DriverDefine.h` 签名里**，改名会立刻产生连锁。两处选择基于同一判据（改在哪一侧代价更低），结论不同是合理的。

**代价（须在 tasks 中记录）**：变更 4–7 移植上游调用点时须做一次机械改名（`curMask.forEachSetBit(...)` → `curMask.ForEachSetBit(...)`）。已在 `backend-utils` spec 的映射约定表中登记，避免逐个文件重新讨论。

**两处命名特例**（保留上游名）：
- 断言宏 `FILAMENT_CHECK_PRECONDITION` / `FILAMENT_CHECK_POSTCONDITION` / `assert_invariant`——上游调用点出现 38 次，改名会使每次对照上游都要重新翻译（见 D2）
- `MurmurHashFn`——已是 `PascalCase`，无需改

### D2: 断言宏采用「保留上游语义名 + 上游条件短路手法」

上游 Vulkan 代码里 24 处 include `utils/Panic.h`，使用 `FILAMENT_CHECK_PRECONDITION` / `FILAMENT_CHECK_POSTCONDITION` / `assert_invariant` 三种宏，其中 9 处带 `<<` 流式消息：

```cpp
FILAMENT_CHECK_POSTCONDITION(result == VK_SUCCESS)
        << "Unable to create Vulkan debug messenger. error=" << static_cast<int32_t>(result);
```

若不做同名宏，这 24 处每处都要改写为 `LOG_CRITICAL("...", result)` 形式——**这是纯粹的噪音改动，且每次对照上游都要重新翻译一遍**。

**决策**：保留三个宏名，并直接采用上游的条件短路手法（`switch(0) case 0: default:` + 三目 + `Voidify() &&`）：

```cpp
#define UTILS_CHECK_CONDITION_IMPL(condition) \
    switch (0) case 0: default:               \
        (condition) ? (void)0 : ::utils::details::Voidify() &&

#define FILAMENT_CHECK_PRECONDITION(condition) \
    UTILS_CHECK_CONDITION_IMPL(condition) \
    ::utils::PanicStream("PRECONDITION", #condition, __FILE__, __LINE__)
```

条件成立时走 `(void)0`，`PanicStream` **根本不构造**（零开销）；条件失败时构造临时对象，`<<` 累积到 `std::ostringstream`，**全表达式结束时析构** → 经 `utils::Log::Critical` 输出 → `std::abort()`。比 spec 初稿设想的 `do { if (...) Fail(); } while(0)` 更优——后者无法支持 `<<` 链。

上游 `PanicStream` 依赖 `utils/CallStack.h`（调用栈打印）与 `utils/sstream.h`，本项目**不引入**——用 `std::ostringstream` 替代，放弃调用栈打印。

**关键修正（实施阶段发现）**：`assert_invariant` **不等效**于 `FILAMENT_CHECK_*`，它在上游定义于 `utils/debug.h` 而非 `Panic.h`：

```cpp
// 上游 utils/debug.h
#ifdef NDEBUG
#   define assert_invariant(e) ((void)0)                          // release 下不生效
#else
#   define assert_invariant(e) \
        (UTILS_VERY_LIKELY(e) ? ((void)0) : utils::panic(__func__, __FILE__, __LINE__, #e))
#endif
```

| 宏 | 宿主头文件 | release 下 | 流式 `<<` |
|---|---|---|---|
| `FILAMENT_CHECK_PRECONDITION` | `Utils/Panic.h` | 生效 | 支持 |
| `FILAMENT_CHECK_POSTCONDITION` | `Utils/Panic.h` | 生效 | 支持 |
| `assert_invariant` | `Utils/Debug.h` | **不生效**（`((void)0)`） | **不支持** |

上游 Vulkan 代码中 `assert_invariant` 的调用点**零处**使用 `<<`（grep 确认），故不需流式设施。本决策据此把 `assert_invariant` 划到独立的 `Utils/Debug.h`，与 `Utils/Panic.h` 解耦。

### D3: `ThreadSafeResource` 派生自 `Resource`，而非平行基类

上游 `ThreadSafeResource` 与 `Resource` 是**两个平行基类**（`resource_ptr<T>` 用 `if constexpr` 区分）。本项目若照搬平行结构，`ResourceManager::Acquire<D>()` / `Destroy()` 全部要加分支，`DECLARE_SHARE_PTR_CLASS` 也要两套。

决策：让 `ThreadSafeResource` 继承 `Resource`：

```cpp
// Resource.h
struct Resource : public NS_UTILS::Ref {
    // ... 现有成员不变
};

// 语义标记：仅表示该类型的销毁须经线程安全队列入队，
// 引用计数沿用 Ref 的原子实现，不引入第二套计数
struct ThreadSafeResource : public Resource {};
```

`ResourceManager` 侧只需判断 `is_base_of_v<ThreadSafeResource, D>` 决定入哪个队列，`Acquire` / `Destroy` / `HandleCast` 全部零改动。

**已否决的替代方案**：照搬上游平行基类。否决理由——本项目 `SharedPtr` 与 `HandleAllocator` 的既有约定是「所有资源型对象都是 `Resource`」，引入平行基类会打破这条约定，且改动面远大于收益。

### D4: `ResourceManager` 双队列的具体形态

```cpp
// ResourceManager.h
using GcList = std::vector<std::pair<ResourceType, HandleBase::HandleId>>;

template <typename D>
static constexpr bool kIsThreadSafe = std::is_base_of_v<ThreadSafeResource, D>;

void destructLaterWithType(ResourceType type, HandleBase::HandleId id);

std::mutex m_threadSafeGcListMutex;
GcList     m_threadSafeGcList;
std::mutex m_gcListMutex;
GcList     m_gcList;
```

`destructLaterWithType` 按 `isThreadSafeType(type)` 分派到对应列表——**分类依据是 `ResourceType` 而非模板参数**，因为 `Resource::OnLastRef()` 只拿得到运行期的 `m_type`。

`isThreadSafeType` 用 `switch` + `CASE_FROM_TO`：

```cpp
constexpr bool isThreadSafeType(ResourceType type) noexcept {
    switch (type) {
        CASE_FROM_TO(ResourceType::Program,    true)
        CASE_FROM_TO(ResourceType::Fence,      true)
        CASE_FROM_TO(ResourceType::Sync,       true)
        CASE_FROM_TO(ResourceType::TimerQuery, true)
        default: return false;
    }
}
```

`Gc()` 与 `Terminate()` 须排空**两个**列表：`Terminate()` 现有实现是「循环 `Gc()` 直到 `m_gcList` 为空」，须改为「两个列表都空」。

**顺序约束**：`Gc()` 先处理线程安全队列再处理普通队列。理由——线程安全队列里的对象（`VulkanFence` 等）可能被普通队列对象的析构路径引用，先处理可减少一次遍历。此顺序不构成正确性依赖，仅作约定。

### D5: `CallbackHandler` 是纯接口，`CallbackManager` 才是有状态件

上游分工（本项目沿用）：

```
backend/CallbackHandler.h   纯接口，app 侧实现（JobSystem 版 / 立即执行版 / 空实现版）
       ↑ 后端只持指针调用 handler->post(callback, userData)
src/CallbackManager.{h,cpp} 后端内部：句柄化登记，编译完成时批量派发
src/CompilerThreadPool.{h,cpp} 后端内部：N 线程 + 优先级队列 + Condition 等待
```

`VulkanSync::CallbackData` 持有 `CallbackHandler*` 但不拥有它——这一点在移植 `VulkanAsyncHandles` 时必须保持，否则会出现 app 侧 handler 已析构、后端仍持有悬垂指针。

### D6: `utils::JobSystem` 空实现，不做完整移植

上游 `VulkanPipelineCache.cpp` 对 `JobSystem` 的使用只有：

```cpp
JobSystem::setThreadName("CompilerThreadPool");
JobSystem::setThreadPriority(JobSystem::Priority::DISPLAY);
```

且这两行位于 `CompilerThreadPool` 的工作线程启动处。而 `CompilerThreadPool` **自带 `std::thread`**，与 Filament 的 `JobSystem` 无关。

决策：新建 `src/JobSystem.h`，提供 `SetThreadName(const char*)` / `SetThreadPriority(Priority)` 两个空实现（macOS 可选用 `pthread_setname_np` 让调试器能识别线程名）。不移植 Filament 的 `JobSystem` 全套（工作窃取调度器 1000+ 行）。

### D7: `tsl::robin_map` → `std::unordered_map` 的替换约束

用户决策。7 个使用文件在后续变更中逐个移植，本变更只固化约束：

| 约束 | 说明 |
|---|---|
| 类型别名 | 每处用 `using XxxMap = std::unordered_map<K, V, Hash>;` 具名，不裸写 |
| 自定义哈希 | 上游 `tsl::robin_map<K,V,Hash,Equal>` 的 `Hash` 保留，`Equal` 用 `std::equal_to`（下游多数文件显式传 `PipelineEqual`，须保留） |
| `erase` 语义核对 | `robin_map::erase` 会造成向后移位；`std::unordered_map::erase` 只失效被删元素。**须逐文件核对是否存在"erase 后持有其他元素迭代器"的模式**，已知 `VulkanPipelineCache::gc()` / `resetBoundPipeline()` 两处重点检查 |
| 引用稳定性 | `unordered_map` rehash 时引用稳定、迭代器失效。若代码跨 rehash 持有迭代器须改为持有 key 或指针 |
| 性能预期 | 管线缓存查找从开放寻址退到链式哈希，实测差距通常在可接受区间；不作为否决理由，但须在变更 6 记录实测值 |

### D8: 验证策略——纯增量的自证方式

本变更不引入任何行为变化，因此验证重点是**不回归**而非新功能：

1. `cmake --build build` 全绿
2. `bin/BackendTests` 仍 exit 0、无 `LOG_CRITICAL`
3. 为新增的 `Bitset` / `RangeMap` / `Range` / `hash::MurmurHashFn` / 两个断言宏在 `3rd/Utils/tests/` 补单元测试（子模块内），跑 `bin/UtilsTests`
4. `ThreadSafeResource` 的入队路径在组件落地前**无调用方**，只能以「编译通过 + 分类表覆盖 `ResourceType` 中四个线程安全类型」为静态验证；运行时验证留给变更 6

第 4 点是本变更的固有局限，须写进 tasks 的复核项，不能宣称"已验证"。

### D9: `EARLY_RETURN` 双份定义清理

`src/Macro.h` 与 `3rd/Utils/include/Utils/Macro.h` 都定义了 `EARLY_RETURN`，展开体一致。两处都 include 时后包含者覆盖前者，目前靠展开体相同掩盖。

决策：删除 `src/Macro.h` 中的定义，只保留 `3rd/Utils` 一份。`src/Macro.h` 若无其他内容则一并删除（当前该文件仅此一个定义）。

风险：若有 `src/` 下的文件只 include `"Macro.h"` 而没 include Utils 版本，会编译失败。影响面可控（编译器会直接报错），在 tasks 中单列一步处理。

## Risks / Trade-offs

- [`3rd/Utils` 改动跨仓库] → 子模块内单独提交 + 本项目记录指针更新；两份制品在 tasks 中分列，避免混提交
- [`FILAMENT_CHECK_*` / `assert_invariant` 保留上游名与本项目 `LOG_*` 命名规范冲突] → 代码规范要求命名统一，但此处优先「上游调用点零改写」（38 处调用）；在 `Utils/Panic.h` / `Utils/Debug.h` 内写清用途，并在 spec 的适配约束中登记为特例
- [`assert_invariant` 在 release 下失效，而 `FILAMENT_CHECK_*` 生效——易被当成同一类宏误用] → 两者分属 `Utils/Debug.h` 与 `Utils/Panic.h`，并在两个头文件与 spec 中用对照表写明差异；`assert_invariant` 只用于「不变式」而非「参数合法性守卫」
- [新引入的 `Bitset` / `RangeMap` 用项目命名，使变更 4–7 移植上游调用点须机械改名] → 已在 spec 的映射约定表中逐项登记（`forEachSetBit` → `ForEachSetBit` 等），改名只发生一次且机械；改名成本落在「尚未移植的文件」一侧，比改 `DriverDefine.h` 已定稿的签名更低
- [`Bitset` 与既有 `std::bitset` 用法并存] → 既有 `VulkanCommandBufferPool` 用 `std::bitset<kMaxCommandBuffers>`，本变更**不改**它；新组件用 `Bitset*`，两套并存但边界清晰（既有不动、新增用新）
- [`PanicStream` 用 `std::ostringstream` 替代上游的自研 sstream，放弃调用栈打印] → 诊断信息质量下降但不影响正确性；若将来需要调用栈，作为独立变更补 `Utils/CallStack.h`
- [`ThreadSafeResource` 派生自 `Resource` 与上游平行结构不一致] → 记录在案；后续对照上游 `fvkmemory` 时须知道这是有意偏离（D3）
- [`std::unordered_map` 替换引入迭代器失效 bug] → D7 规定逐文件核对 `erase` 语义，`VulkanPipelineCache` 的 `gc()` / `resetBoundPipeline()` 为重点；变更 6 落地时须实测管线缓存命中路径
- [`Bitset` 与既有 `std::bitset` 的精确尺寸差异] → `Bitset32` / `Bitset64` 有编译期断言保证 `sizeof` 分别为 4 / 8，可安全用作命令体成员；既有 `std::bitset` 的用法不受影响
- [`ResourceManager::Terminate()` 改为排空双列表] → 现有 `Terminate()` 只循环 `Gc()`，改动后语义仍为幂等；须确认 `m_threadSafeGcList` 在两次 `Gc()` 之间可被并发追加（有独立 mutex，安全）
- [JobSystem 空实现导致调试器看不到编译线程名] → 非功能性影响；macOS 下可实现 `pthread_setname_np`，代价 2 行

## Open Questions

- `FeatureFlagManager` 的最小形态边界：`VulkanDriver` 构造期只用它判断若干特性开关，是否本次就建立最小可用类，还是留到变更 7 随 `VulkanDriver` 一起补（倾向后者，本变更不引入无调用方的类型）
- `CallbackHandler` 的本地实现版本（立即执行版 / 线程池版）是否需要一并提供：上游由 app 侧提供，本项目在 `tests/` 里需要一个最小实现才能跑异步路径（留到变更 6 需要时补）
- `utils::Invocable` → `std::function` 的替换对 `CompilerThreadPool` 的任务入队是否有性能影响：上游 `Invocable` 是小缓冲优化可调用体，`std::function` 可能堆分配。任务入队频率远低于渲染热路径，预计无影响，变更 6 记录实测
- 是否需要在 `3rd/Utils` 补 CI/构建配置以运行新增的单元测试：`3rd/Utils/tests/` 已存在且 `bin/UtilsTests` 已可执行，预计零改动

## 实施记录

本节记录实施阶段产生、供后续变更直接引用的清单与局限。

### 变更 4–7 的命名迁移清单

本变更新引入的通用容器一律用项目 `PascalCase`，上游调用点须按下表机械改名（原因见 D1）：

| 上游 | 本项目 |
|---|---|
| `mask.forEachSetBit(fn)` | `mask.ForEachSetBit(fn)` |
| `mask.set(b)` / `unset(b)` / `test(b)` / `count()` / `reset()` / `flip(b)` / `firstSetBit()` / `any()` / `none()` / `all()` | `Set` / `Unset` / `Test` / `Count` / `Reset` / `Flip` / `FirstSetBit` / `Any` / `None` / `All` |
| `mask.getValue()` / `setValue(v)` / `getBitsAt(n)` | `GetValue` / `SetValue` / `GetBitsAt` |
| `map.add(f, l, v)` / `get(k)` / `clear(f, l)` / `rangeCount()` | `Add` / `Get` / `Clear` / `RangeCount` |
| `range.first` / `range.last` | `range.First()` / `range.Last()` |
| `hash::murmur3(...)` / `hash::combine(seed, v)` | `hash::Murmur3(...)` / `hash::Combine(seed, v)` |
| `handler->post(user, cb)` | `handler->Post(user, cb)` |
| `driver.purge()` / `scheduleCallback(...)` / `setUnrecoverableError()` / `debugCommandBegin(...)` / `debugCommandEnd(...)` | `Purge` / `ScheduleCallback` / `SetUnrecoverableError` / `DebugCommandBegin` / `DebugCommandEnd` |

**名称已合规、直接沿用**：`hash::MurmurHashFn<T>`、`RangeMap`、`Bitset`（仅大小写归一）。

### `robin_map` → `std::unordered_map` 落地清单（变更 5 / 6 引用）

7 个使用文件及其哈希函数（均保留自定义 Hash 与 Equal）：

| 文件 | 键类型 | 哈希函子 | `erase` 风险 |
|---|---|---|---|
| `VulkanFboCache.h` | `RenderPassKey` / `FboKey` | `hash::MurmurHashFn<...>` | **高**（`Gc()`） |
| `VulkanPipelineCache.h` | `PipelineKey` | `hash::MurmurHashFn<PipelineKey>` | **高**（`Gc()` / `ResetBoundPipeline()`） |
| `VulkanDescriptorSetLayoutCache.h` | `LayoutKey` | `hash::MurmurHashFn<LayoutKey>` | 低（只增） |
| `VulkanDescriptorSetCache.h` | — | — | 中（待核） |
| `VulkanSamplerCache.h` | `Params` | `hash::MurmurHashFn<Params>` | 低（只增） |
| `VulkanYcbcrConversionCache.h` | `Params` | `hash::MurmurHashFn<Params>` | 低（只增） |

替换前三问（每文件逐行核对）：是否在遍历中按 key 删除当前元素？回调是否在遍历期间修改容器？是否跨 rehash 持有迭代器或引用？**若存在 UB 模式，先修正遍历写法再替换容器。**

### 未验证面（须在后续变更兑现）

| 项 | 本变更状态 | 兑现变更 |
|---|---|---|
| `ThreadSafeResource` 的归零入队路径 | **无调用方**——四个线程安全类型（`VulkanProgram` / `VulkanFence` / `VulkanSync` / `VulkanTimerQuery`）均属变更 6。本变更只验证了分类表覆盖与编译通过，**不得声称该路径已验证** | 6 |
| `DriverBase` 的 ServiceThread 回调派发 | 无调用方——`CallbackManager` 在变更 6 才首次使用；本变更只验证了构造/析构的线程启停 | 6 |
| `WaitForFence` / `SignalFence` / `SetUnrecoverableError` | 无调用方——变更 7 的 `fenceWait` 才使用 | 7 |
| `CompilerThreadPool` 的并行编译 | 无调用方——变更 6 的 `VulkanPipelineCache` 才使用 | 6 |
| `scheduleDestroy(BufferDescriptor&&)` | **未移植**——依赖变更 2 的 `BufferDescriptor` | 2 |

### 实施中偏离 spec 的决策（已回填 spec）

1. **`DriverBase` 从「骨架」升为完整移植**（用户决策 A）：含 ServiceThread、`CallbackData` 内联存储池、`Purge`、`WaitForFence` / `SignalFence` / `SetUnrecoverableError`。原因是 `CallbackManager` 依赖其 `ScheduleCallback` 实现，骨架版本无法编译。
2. **`Driver` 接口追加 5 个虚函数**：原计划属变更 7，因 `DriverBase` 的编译前提而前移到本变更（契约见新增的 `driver-interface` delta spec）。
3. **`VulkanDriver` 提前继承 `DriverBase`**：否则 `Driver` 的 4 个新纯虚函数无人实现。变更 7 本就要做这一步。
4. **`CompilerPriorityQueue` 前移到本变更**：`CompilerThreadPool` 的三条优先级队列需要它。本属变更 2。
5. **`construct<D, B>` 不加 `if constexpr` 分支**：`ThreadSafeResource` 派生自 `Resource`，`Init<D>` 是继承成员，两类资源走同一路径。上游需要分支是因为其 `ThreadSafeResource` 是平行基类（见 D3）。
6. **`src/Macro.h` 删除**：其唯一内容 `EARLY_RETURN` 与 `Utils/Macro.h` 重复。删除后修正了 `src/Common.h` / `src/vulkan/VkDef.h` / `src/vulkan/VulkanHandle.cpp` 三处 include。
