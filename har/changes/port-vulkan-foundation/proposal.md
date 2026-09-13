# Change: port-vulkan-foundation

## Why

上游 Vulkan 后端的 17 个组件共享三样本项目尚不具备的底层设施：

1. **`utils/` 缺口**：上游 `src/vulkan/` 逐文件引用 `utils/Hash.h`（7 处）、`utils/bitset.h`（7 处）、`utils/Panic.h`（24 处）、`utils/debug.h`（6 处）、`utils/RangeMap.h`（1 处）、`utils/StructureOfArrays.h`（1 处）。项目 `3rd/Utils` 现有 29 个头文件，前五个全部缺失（`RangeMap` 还连带依赖 `utils/Range.h`，故实际需新增 6 个）。
2. **异步底座缺口**：`VulkanPipelineCache` 依赖 `CallbackManager` + `CompilerThreadPool`；`VulkanSync` / `VulkanFence` / 全部 `create*Async` 方法签名依赖 `CallbackHandler`；`VulkanDriver` 上游继承 `DriverBase`。
3. **`ThreadSafeResource` 缺口**：`VulkanProgram` / `VulkanFence` / `VulkanSync` / `VulkanTimerQuery` 四个类型均继承 `fvkmemory::ThreadSafeResource`（独立 GC 队列 + 线程安全入队），而本项目 `ResourceManager` 明确简化掉了这一支。

这三样是后续 6 个变更的公共前置。同时 `tsl::robin_map` 的替代方案（用户决策：换 `std::unordered_map`）必须在此一次定死，否则 7 个使用文件会各自抉择出不一致的容器。

## What Changes

### 1. `3rd/Utils` 新增通用设施（子模块内提交，6 个头文件）

| 文件 | 内容 | 上游对应物 |
|---|---|---|
| `Utils/Hash.h` | `hash::Murmur3` / `MurmurSlow` / `Combine` / `CombineFast` + 模板函子 `MurmurHashFn<T>` | `utils/hash` 命名空间（7 个使用哈希容器的文件全部依赖 `MurmurHashFn`） |
| `Utils/Bitset.h` | 模板 `Bitset<T, N>` + 别名 `Bitset8/32/64/128/256` | `utils::bitset<T, N>` |
| `Utils/Panic.h` | `FILAMENT_CHECK_PRECONDITION` / `FILAMENT_CHECK_POSTCONDITION`（**release 下仍生效**，支持 `<<` 流式） | `utils/Panic.h` |
| `Utils/Debug.h` | `assert_invariant`（**release 下展开为 `((void)0)`**，不支持流式） | `utils/debug.h` |
| `Utils/Range.h` | `Range<T>`：半开区间 `[first, last)` + 随机访问迭代器 | `utils/Range.h` |
| `Utils/RangeMap.h` | `RangeMap<Key, Value>`：有序不重叠区间容器，自动分裂合并 | `utils/RangeMap.h` |

命名遵循项目 `PascalCase`（用户决策）——类型与公有方法均改名，代价是变更 4–7 移植上游调用点时须做一次机械改名（已在 spec 的映射约定表中登记）。

`assert_invariant` 与 `FILAMENT_CHECK_*` 分属两个头文件，语义不同（前者 debug-only、无流式；后者始终生效、有流式）——这一点在 spec 初稿中写错，实施阶段对照上游 `utils/debug.h` 后更正。

- 清理 `EARLY_RETURN` 在 `src/Macro.h` 与 `3rd/Utils/include/Utils/Macro.h` 的双份定义（保留 Utils 一份）

### 2. 后端异步底座（本项目内）

- 新增 `include/Backend/CallbackHandler.h`：纯接口，app 侧实现，后端只持指针调用
- 新增 `src/CallbackManager.{h,cpp}`：句柄化回调登记与批量派发
- 新增 `src/CompilerThreadPool.{h,cpp}`：并行着色器编译线程池
- 新增 `src/DriverBase.h`：`debugCommandBegin` 钩子 + `DriverConfig` 派生字段承载
- `utils::JobSystem` 仅提供 `SetThreadName` / `SetThreadPriority` 空实现（上游在 `VulkanPipelineCache.cpp` 只用这两个）

### 3. `ThreadSafeResource` 回补（BREAKING：资源层语义扩展）

- `Resource.h` 新增 `struct ThreadSafeResource : public Resource`：仅作语义标记，引用计数沿用 `NS_UTILS::Ref` 的 `std::atomic<int32_t>`
- `ResourceManager` 新增 `mThreadSafeGcList` + `mThreadSafeGcListMutex`、`isThreadSafeType()` 分类表；`construct()` 增加 `if constexpr (is_base_of_v<ThreadSafeResource, D>)` 分支；`Gc()` / `Terminate()` 排空两个列表
- `ResourceType` 中属于线程安全的类型（`Fence` / `Sync` / `TimerQuery` / `Program`）SHALL 登记进分类表

### 4. 映射约定固化（记录在 design，不产生独立制品）

| 上游 | 本项目 |
|---|---|
| `tsl::robin_map` | `std::unordered_map`（用户决策） |
| `utils::Invocable` | `std::function` |
| `utils::Condition` | `std::condition_variable` |
| `utils::Mutex` | `std::mutex` |
| `utils::CString` / `ImmutableCString` | `NS_UTILS::String` / `NS_UTILS::ImmutableString` |
| `utils::FixedCapacityVector` | `std::vector` |
| `utils::bitset32` / `bitset64` | `NS_UTILS::Bitset32` / `Bitset64` |
| `utils::JobSystem` | 空实现（仅线程命名/优先级） |
| `BlueVK` | `volk` |

**不移植**：`JobQueue` / `WorkStealingJobQueue`（Vulkan 后端零引用）、`utils::io::ostream` 体系、`ComputeTest` / `DataReshaper` 等前端专用件、`FeatureFlagManager` 完整实现（仅保留最小形态）。

## Capabilities

### New Capabilities

- `backend-utils`: `hash::MurmurHashFn` 哈希函子 / `Bitset<T, N>` 模板与宽度别名 / `FILAMENT_CHECK_*` 断言宏 / `assert_invariant` debug 断言 / `Range<T>` 与 `RangeMap`，以及 std 映射约定
- `backend-async`: `CallbackHandler` 接口、`CallbackManager`、`CompilerThreadPool`、`DriverBase`、`JobSystem` 空实现

### Modified Capabilities

- `vulkan-resource`: 新增 `ThreadSafeResource` 语义与双 GC 队列；`ResourceManager` 构造/回收路径扩展

## Impact

- 新增（子模块 `3rd/Utils`，需在子模块内单独提交）：
  - `include/Utils/Hash.h`、`include/Utils/Bitset.h`、`include/Utils/Panic.h`、`include/Utils/Debug.h`、`include/Utils/Range.h`、`include/Utils/RangeMap.h`
- 新增（本项目）：
  - `include/Backend/CallbackHandler.h`
  - `src/CallbackManager.h` / `CallbackManager.cpp`
  - `src/CompilerThreadPool.h` / `CompilerThreadPool.cpp`
  - `src/DriverBase.h`
  - `src/JobSystem.h`（空实现）
- 修改：
  - `src/vulkan/resource/Resource.h`（+`ThreadSafeResource`）
  - `src/vulkan/resource/ResourceManager.h` / `.cpp`（双 GC 列表 + 分类表 + `construct` 分支）
  - `src/Macro.h`（删除重复的 `EARLY_RETURN`）
  - `include/Backend/DriverDefine.h`（`DriverConfig` 增补 `disableHandleUseAfterFreeCheck` / `disableHeapHandleTags`）
- CMake：新增源文件由 `file(GLOB_RECURSE src/*.cpp)` 自动收集；`3rd/Utils` 的头文件由既有 `target_include_directories` 覆盖，构建脚本零改动
- 不改：`Utils::Ref`（已用 `std::atomic<int32_t>`，无需改动）、`HandleAllocator`、`SharedPtr`
- 提交约束：`3rd/Utils` 的改动须在子模块仓库单独提交，本项目仓库记一次子模块指针更新（见 `.dsh/rules/git.md`）
