# Design: Port Allocator from Filament

## Context

- 参考源：`/Users/turiing/filament/libs/utils/include/utils/Allocator.h`（985 行）与 `src/Allocator.cpp`（225 行），Apache-2.0，生产验证实现
- 目标库：`/Users/turiing/Backend/3rd/Utils`（命名空间 `utils`，头文件前缀 `Utils/`，`#pragma once`，函数 PascalCase，C++20）
- 现有 Utils 设施：`Compiler.h`（有 `UTILS_LIKELY/UNLIKELY/NOINLINE` 与线程注解宏，缺 `UTILS_RESTRICT`/`UTILS_MUL_OVERFLOW`）、`Macro.h`（平台宏）、`thread/lock/LockGuard.h`（`std::lock_guard` 子类 + 注解）、`tests/`（gtest）
- 移植需求（用户指定）：Arena 去 TrackingPolicy；AllocatorPolicy/AreaPolicy 有基类并在模板中验证派生类；pointermath 移入 `utils::pointer` 命名空间存于 `Pointer.h`
- 已确认决策（探索阶段用户拍板）：基类命名方案 B（`*Base` 后缀）；取消 `AreaPolicy` 命名空间；命名转 PascalCase；Arena 删除 name 参数；头文件拆多个存放于 `Arena/` 目录

## Goals / Non-Goals

**Goals:**
- 全家族忠实移植（LinearAllocator/HeapAllocator/WithFallback/FreeList/AtomicFreeList/PoolAllocator/Arena/ArenaScope/STLAllocator/Area/LockingPolicy/Pointer）
- 按需求完成三项改造，接口风格与 Utils 库现有规范统一（PascalCase、`#pragma once`、无注释噪声）
- 补齐编译依赖（MemAlign、Mutex 别名、缺失宏）且不动既有代码行为
- 移植附 gtest 单测，验证分配语义与线程安全链表

**Non-Goals:**
- 不移植 TrackingPolicy 及 `Arena.cpp` 中 HighWatermark/Debug 实现（随需求删除）
- 不移植 pthread `utils::Mutex` 完整封装（用 `std::mutex` 别名替代）
- 不引入虚函数多态、不改 Arena 按值持有 policy 的存储模型
- 不修复 filament 原实现的设计取舍（如 LinearAllocator 4GB 上限、`PoolAllocatorWithFallback` 部分缺失接口），仅注释说明

## Decisions

### D1: 基类为空标记类，无虚函数

`AllocatorPolicyBase` / `AreaPolicyBase` 定义为空类，所有派生类公有继承。理由：

- Arena 按**值**持有 `mAllocator`/`mArea`，且是模板（编译期分派），虚函数在此模型下永远不会被分派，只会引入 vtable 与破坏空基类优化
- `std::is_base_of_v` 是编译期纯类型检查，零运行时开销，正是"验证模板参数"需求的正确工具
- 备选方案（带纯虚接口的基类）被否决：语义上是"概念契约"，但 C++ 概念（concept）层面由 static_assert + 鸭子类型已足够，虚接口是纯负担

### D2: 基类命名 `*Base`，模板参数名保持不变

```cpp
class AllocatorPolicyBase { };
class LinearAllocator : public AllocatorPolicyBase { ... };

template<typename AllocatorPolicy, typename LockingPolicy, typename AreaPolicy = HeapArea>
class Arena {
    static_assert(std::is_base_of_v<AllocatorPolicyBase, AllocatorPolicy>, "...");
    static_assert(std::is_base_of_v<AreaPolicyBase, AreaPolicy>, "...");
};
```

理由：调用处 `Arena<LinearAllocator, NoLock, HeapArea>` 读起来语义完整；`*Base` 后缀明确表示"约束标记"。备选方案（基类占 `AllocatorPolicy` 名、模板参数缩写为 `AP/ARP`）可读性差，否决。

### D3: 取消 `AreaPolicy` 命名空间，Area 上提为平级类

原 `AreaPolicy::StaticArea/HeapArea/NullArea` → 平级类继承 `AreaPolicyBase`。理由：用户明确要求；命名空间在类型体系里无法参与继承，与"AreaPolicy 有基类"冲突。`LockingPolicy` 保持命名空间（它是策略集合而非单一策略类型，用户未要求改造）。

### D4: 函数命名转 PascalCase

全库统一（与 `CircularBuffer` 等现有头文件一致）：`alloc→Alloc`、`free→Free`、`reset→Reset`、`rewind→Rewind`、`make→Make`、`destroy→Destroy`、`pop/push→Pop/Push`、`data/begin/end/size→GetData/GetBegin/GetEnd/GetSize`、`pointermath::add/align→pointer::Add/Align`。**例外**：`STLAllocator::allocate/deallocate` 是 C++ 标准分配器接口要求，保持原名。`Arena::alloc<T>(count, ...)` 的 SFINAE 约束与乘法溢出防护逻辑原样保留。

### D5: 头文件拆分存放于 `include/Utils/Arena/`

按类族拆 8 个头文件 + 2 个 cpp（见 Impact）。理由：985 行单文件不利于维护；`Arena/` 目录收敛整个能力域。include 前缀统一 `Utils/Arena/Xxx.h`。`Pointer.h` 与 `mem/MemAlign.h` 因属通用工具放对应层级。CMake 与 tests 均为 `GLOB_RECURSE`，新增文件零配置。

### D6: Arena 删除 TrackingPolicy 与 name 参数

删除 `TrackingPolicy` 模板参数、`mListener`、全部 `onAlloc/onFree/onReset/onRewind` 调用、`getListener/setListener/emplaceListener`、swap 中的 listener 交换。连带删除 `mArenaName`、name 构造参数与 `getName()`。构造签名变为 `Arena(size, args...)` / `Arena(AreaPolicy&&, args...)`。锁保护（`std::lock_guard<LockingPolicy>`）保留。

### D7: 依赖补齐的落点

- `UTILS_RESTRICT` → `Compiler.h`（与 `UTILS_LIKELY` 同层，GCC/Clang 为 `__restrict`，否则为空）
- `UTILS_MUL_OVERFLOW` → `Compiler.h`（GCC/Clang 走 `__builtin_mul_overflow`，否则 fallback 乘法判溢出）
- `UTILS_MAX` 不移植宏：`ObjectPoolAllocator` 改用 `std::max`（constexpr 可用）
- `AlignedAlloc/AlignedFree` → 新建 `mem/MemAlign.h`（POSIX `posix_memalign` / Windows `_aligned_malloc`，原名 `aligned_alloc/aligned_free` 与 C17 标准函数同名易混淆，故转 PascalCase 前缀）
- `LockingPolicy::Mutex` → `using Mutex = std::mutex`，配合现有 `LockGuard`（已是 `std::lock_guard<M>` 子类并携带线程注解）使用，不移植 pthread 封装

### D8: 移植忠实度边界

保留 filament 的反直觉设计并注释说明：
- `LinearAllocator` 以 `uint32_t` 存 `mSize/mCur`（区间上限 4GB，压缩内存）
- `AtomicFreeList::HeadPtr` 32 位 offset + 32 位 tag 凑 8 字节原子（tag 防 ABA），Pop/Push 的 acquire/release CAS 语义保留
- `alloc` 的 branch-less 写法（先算后判）保留

## Risks / Trade-offs

- [AtomicFreeList 并发正确性依赖 CAS/tag 细节] → 原实现含防御断言（`pNext >= pStorage` 等）；移植后加 gtest 多线程压力测试 + TSAN 构建验证
- [PascalCase 批量改名引入笔误/漏改] → 测试编译即接口契约；`STLAllocator` 例外项单独核对
- [`UTILS_MUL_OVERFLOW` 非 GCC/Clang 平台 fallback 可能漏判] → fallback 用除法回验溢出，仍错误时断言
- [4GB 上限（uint32_t）对超大 arena 是硬限制] → 与 filament 一致，头文件注释声明；需要时后续可模板化偏移类型
- [取消 `AreaPolicy` 命名空间后 `AreaPolicy` 标识符消失] → 全新移植无外部使用者，文档（spec/design）已记录映射

## Open Questions

- 测试范围：是否补充 `LinearAllocatorWithFallback`/`PoolAllocatorWithFallback` 的堆块泄漏断言（用 `Allocated()` 前后对比）？默认计划覆盖
- `GetBegin/GetEnd` vs `Begin/End`：Area 访问器命名按 `GetXxx` 统一（与 `GetData/GetSize` 一致），如有异议可在 apply 阶段调整
