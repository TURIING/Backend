# Design: 移植 Filament HandleAllocator

## Context

- 源文件：`/Users/turiing/filament/filament/backend/include/private/backend/HandleAllocator.h`（407 行，模板定义）与其实现 `backend/src/HandleAllocator.cpp`（277 行，非模板成员 + 显式实例化）
- 目标：`src/HandleAllocator.h` + `src/HandleAllocator.cpp`（`Backend` 命名空间）
- 本项目现状：
  - `include/Backend/Handle.h`：`HandleBase::HandleId = uint32_t`、`Handle<T>`，API 与 Filament 兼容（`GetId`/`operator bool`），可直接复用
  - `3rd/Utils`：已移植 `Arena` / `PoolAllocator` / `HeapArea` / `ImmutableString` / `Mutex` / `LockGuard` / `UniqueLock` / `Compiler` 宏 / `Log`（spdlog 风格），命名空间 `utils`，API 为 PascalCase
  - `allocator` spec 明确：本项目 Arena **无 name 构造参数、无 TrackingPolicy**；`AllocatorPolicyBase` 为纯虚基类，`Arena` 经 concept 约束
  - 项目无 `tsl/robin_map`、无 `assert_invariant` / `FILAMENT_CHECK_POSTCONDITION` / `PANIC_LOG` / `LOG(WARNING)` 流式宏
  - 根 CMakeLists 以 `file(GLOB_RECURSE src/*.cpp)` 收集源码，src 为 PRIVATE include 目录
- 关键事实（已核源码确认）：Filament 的 age 完全由 HandleAllocator 自己读/写（分配时读池块 OFFSET 预留区的 `pNode[-1].age`，释放时校验后递增），`TrackingPolicy::DebugAndHighWatermark` 只做高水位统计与调试字节填充，不参与 age——因此本项目无 TrackingPolicy 不影响 age 语义迁移

## Goals / Non-Goals

**Goals:**
- 忠实移植 HandleAllocator 的池化分配、age use-after-free 检测、慢路径、调试标签全部语义
- 全部 API 适配本项目 Utils 命名与约束，编译零改动通过
- 符合项目代码规范（命名、include 顺序、注释重写、位运算封装）

**Non-Goals:**
- 不接入 `VulkanDriver`（已确认：本变更仅落库组件，接入另行变更）
- 不补充 `tests/` 冒烟测试（已确认）
- 不移植 GL/MTL/WGPU 实例（仅 Vulkan `HandleAllocator<64, 160, 312>`）
- 不引入 `tsl/robin_map` 第三方依赖
- 不修改 `3rd/Utils`、`include/Backend/Handle.h` 与根 CMakeLists

## Decisions

### D1: 文件位置与命名空间
`src/HandleAllocator.h` + `src/HandleAllocator.cpp`（src 已是 PRIVATE include 目录；GLOB 自动纳入 .cpp）。命名空间 `Backend`（`BEGIN_NS_BACKEND`），内部引用 `utils::` 设施。
**理由**：HandleAllocator 是通用后端组件，不属于 command/vulkan 子目录；与 `src/PlatformFactory.cpp` 同级。

### D2: 内嵌 Allocator 策略继承 `utils::AllocatorPolicyBase`
本项目 `Arena` 有 `requires AllocatorPolicyDerivable<AllocatorPolicy>`（`is_base_of_v<AllocatorPolicyBase>`）约束，Filament 的鸭子类型策略不可直接用。适配：
- 继承 `utils::AllocatorPolicyBase`，实现纯虚 `Alloc(size, alignment)` / `Free(p, size)`（2 参 override）
- 保留 3 参非虚重载 `Alloc(size, alignment, extra)`（Arena::Alloc 3 参版本调用；内部按 size 选桶后 `mPoolX.Alloc(size, alignment)`，忽略 extra——池的 OFFSET 在模板参数固定为 `sizeof(Node)`）
- `Allocator(const HeapArea& area, bool disableUseAfterFreeCheck)` 构造（`Arena(size, flag)` → `m_allocator(m_area, flag)` 直通）
**理由**：满足 concept 约束且保持池选择逻辑不变；虚调用仅发生在 2 参交集，3 参快路径无虚开销。

### D3: Arena 构造与 age 读写适配
- 构造：`mHandleArena(size, disableUseAfterFreeCheck)`（本项目 `Arena(size_t, args...)`，无 name 参数；无 TrackingPolicy）
- 分配：`mHandleArena.Alloc(SIZE, alignof(std::max_align_t), 0)` 返回后，由 HandleAllocator 读取 `((Allocator::Node*)p)[-1].age` 作为 age 打包进句柄 id（`Allocator` 已 `friend class HandleAllocator`，Node 可访问）
- 释放：age 校验与递增在 `deallocateHandleFromPool` 内完成（id 可解析出期望 age），随后 `mHandleArena.Free(p, SIZE)` 只做加锁池归还；内嵌 `Allocator::Free(p, size)` 不再重复校验
- `.cpp` 构造中保留 `memset(area.GetData(), 0, maxHeapSize)` 清零 age 预留区
**理由**：本项目 Arena 的 `Alloc`/`Free` 不传递 age（无 outAge 参数、无 TrackingPolicy），age 语义闭环上移到 HandleAllocator 层，行为与 Filament 等价。

### D4: 慢路径与调试标签用 `std::unordered_map` 替代 `tsl::robin_map`
`mOverflowMap`（堆句柄登记）与 `mDebugTags`（调试标签）均改用 `std::unordered_map<HandleBase::HandleId, ...>`；`pos.value()` → `pos->second`；`reserve(512)` 保留。
**理由**：两者均为慢路径或调试路径，哈希性能差异可忽略；避免引入新第三方依赖。`mOverflowMap` 仍由 `utils::Mutex` 保护（`UTILS_GUARDED_BY` 注解保留）。

### D5: 断言/日志映射
| Filament | 本项目 | 语义 |
|---|---|---|
| `assert_invariant(cond)` | `LOG_ASSERT(cond)` | debug 断言（NDEBUG 空） |
| `FILAMENT_CHECK_POSTCONDITION(cond) << msg` | `cond` 不成立时 `LOG_CRITICAL(msg)`（fmt 风格，含 size/指针/id 诊断） | 致命（abort） |
| `PANIC_LOG(msg)` | `LOG_ERROR(msg)` | 非致命警告 |
| `LOG(WARNING) << ...` | `LOG_WARN(...)` | 警告 |

**理由**：与项目现有 spdlog 日志体系对齐；`LOG_CRITICAL` 内部 abort，等价于 Filament 检查宏的终止语义。

### D6: 位运算与常量
保留 `HANDLE_AGE_*` / `HANDLE_INDEX_MASK` / `HANDLE_HEAP_FLAG` 为 `static constexpr`（已是命名常量，含义自明）；`key &= ~(HANDLE_DEBUG_TAG_MASK ^ HANDLE_AGE_MASK)` 这类一次性组合表达式在 `getHandleTag`/`associateTagToHandle` 两处重复出现，提取为私有 `static constexpr uint32_t HANDLE_TAG_KEY_MASK`（语义：去掉 age 与 debug tag 位段），避免散落重复位运算。
**理由**：符合 cpp 规范（位操作封装、不散落魔法位掩码）。

### D7: 实例化策略
头文件以 `using HandleAllocatorVK = HandleAllocator<64, 160, 312>;` 定义别名（替换 Filament 的 4 个宏），`.cpp` 显式实例化 `template class HandleAllocator<64, 160, 312>;`。
**理由**：类型别名用 `using`（cpp 规范），本项目仅 Vulkan 后端，其余宏定义无用途不留。

### D8: 注释与规范
删除 Filament license/文件头注释与所有说明性注释，按项目注释规范重写：仅保留解释"为什么/意图"的注释（如 age 预留区偏移、debug tag 截断原因、池分配版本化的 NOINLINE 理由），函数体注释 ≤3 条。命名遵循 PascalCase/camelCase/m_ 前缀规则；`Log.h` 方法名对齐本项目。

## Risks / Trade-offs

- [OFFSET 预留区被误删导致 `pNode[-1]` 越界读] → 移植时严格保留 `Pool<Pn> = PoolAllocator<Pn, MIN_ALIGNMENT, sizeof(Node)>` 模板参数，并加冒烟测试验证
- [age 校验/递增移到 Arena 锁外，双线程并发释放同一句柄存在极小竞态窗口] → double-free 本身是未定义行为，仅影响检测及时性；Filament 语义等价性不受影响（检测仍生效）
- [GLOB_RECURSE 无 CONFIGURE_DEPENDS，新 .cpp 不被自动发现] → 移植后需手动重新 configure 一次；后续可在 CMake 中补 CONFIGURE_DEPENDS（非本变更范围）
- [`Arena(size, flag)` 构造要求 Allocator 构造签名精确匹配 `(const HeapArea&, bool)`] → 已确认本项目 `Arena(size_t, args...)` → `m_allocator(m_area, args...)` 直通，签名匹配即可
- [`LOG_CRITICAL` 无条件 abort 与 Filament `FILAMENT_CHECK_POSTCONDITION` 的 release 行为差异] → Filament 检查宏在 release 亦启用（编译期条件不同），本项目 `LOG_CRITICAL` 恒启用，语义一致且更严格

## Open Questions

无（已确认：不接入 VulkanDriver；不补充 tests/ 冒烟测试）
