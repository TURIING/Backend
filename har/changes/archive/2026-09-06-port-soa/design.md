# Design: Port SoA from Filament

## Context

- 参考源：`/Users/turiing/filament/libs/utils/include/utils/StructureOfArrays.h`（784 行，头文件模板，Apache-2.0，生产验证）
- 目标库：`/Users/turiing/Backend/3rd/Utils`（命名空间 `utils`，头文件前缀 `Utils/`，`#pragma once`，方法 PascalCase、成员 `m_` 前缀，C++20）
- 现有 Utils 设施：`Arena/Allocator.h`（`AllocatorPolicyBase` 纯虚两参接口 + `HeapAllocator`/`LinearAllocator`/`LinearAllocatorWithFallback`，`AllocatorPolicyDerivable` 等 concept）、`Compiler.h`（有 `UTILS_NOINLINE`/`UTILS_RESTRICT`/`UTILS_LIKELY`/`UTILS_UNLIKELY`，无 `UTILS_ALWAYS_INLINE`/`UTILS_UNROLL`/`UTILS_UNUSED`）、`Utils.h` 伞头聚合全部公共头、`tests/`（gtest，GLOB 收集）
- 参考先例：`2026-08-22-port-circular-buffer`（Filament → Utils 移植）、`2026-08-29-port-allocator`（补齐依赖宏进 `Compiler.h`、命名转 PascalCase 的既有做法）
- 已确认决策（探索阶段用户拍板）：完整保真移植；`SoaBase<Allocator, E...>` + `using Soa = SoaBase<HeapAllocator, E...>`；`slice()` 返回 `std::span`；落点 `include/Utils/Soa.h` 顶层

## Goals / Non-Goals

**Goals:**
- 完整保真移植四层 API：容量/生命周期、按列访问、行代理访问（`IteratorValueRef`/`IteratorValue`）、随机访问迭代器 + `Field` 字段引用 + `CopyRange`
- 类型命名压缩为 `Soa`，函数/成员/注释全部符合 Utils 库规范
- 默认分配器落 `HeapAllocator`，`SoaBase` 保留分配器模板注入能力并约束为 Utils 分配策略契约
- 移植附 gtest 单测覆盖 spec 全部 Scenario，验证布局、生命周期与 STL 集成

**Non-Goals:**
- 不移植 `Slice` 类型（用 `std::span` 替代）、不新增缺失宏到 `Compiler.h`
- 不新增 `allocator` 实例注入构造（现阶段仅支持默认构造的分配策略）
- 不修复 filament 原实现的取舍（`iterator::pointer = IteratorValueRef*` 的语义瑕疵、缩容不回收内存的 TODO），保持行为等价
- 不改动既有代码行为与构建配置；src 无 cpp 产出

## Decisions

### D1: 类型命名与模板形态

```cpp
template <typename Allocator, typename... Elements>
    requires AllocatorPolicyDerivable<Allocator> && std::default_initializable<Allocator>
class SoaBase { ... };

template <typename... Elements>
using Soa = SoaBase<HeapAllocator, Elements...>;
```

保留 Filament 两层结构仅改名（`StructureOfArraysBase`→`SoaBase`、别名→`Soa`）。理由：`template <typename...> class` 无法把 allocator 参数排到包后并提供默认值，别名是唯一让日常使用写 `Soa<E...>`、同时保留注入能力的形态；与 Filament 等价改名、迁移心智成本最低。备选（单层 `Soa<Allocator, E...>` 显式传分配器 / 固定 HeapAllocator 单层）牺牲默认便捷或注入能力，否决。

### D2: 分配器契约——值成员 + 两参 Free

- 内部按值持有 `Allocator m_allocator`；整块缓冲经 `m_allocator.Alloc(size, alignment)` 分配
- 释放统一 `m_allocator.Free(p, size)`（**两参、携带实际尺寸**）：析构时 `Free(基址, GetNeededSize(m_capacity))`，`SetCapacity` 重分配后 `Free(旧基址, 旧容量的 GetNeededSize)`
- 约束复用 Utils 现有 `AllocatorPolicyDerivable` concept + `std::default_initializable`（`AllocatorPolicyBase` 的纯虚 `Alloc(size,align)`/`Free(p,size)` 已保证两参接口存在；三策略的 `Free(void*, size_t)` 均为 override）
- 与 Filament 差异说明：Filament 用一参 `free(ptr)`（其默认 `HeapArena` 自记尺寸）；Utils 策略接口是两参风格，`HeapAllocator` 忽略 size、`LinearAllocator` 为 no-op（arena 拥有权），传递真实尺寸语义更一致
- 移动构造/赋值沿用 Filament 的成员 `std::swap` 方案：`HeapAllocator` 无用户移动但有隐式拷贝，`std::swap` 经拷贝路径成立；`LinearAllocator` 自身提供移动构造/赋值

### D3: `slice<E>()` 返回 `std::span`

`Slice<E>()` 返回 `std::span<TypeAt<E>>`（const 版为 `std::span<const TypeAt<E>>`），`#include <span>`。理由：Utils 无 `Slice` 类型，C++20 已启用，span 是标准替代且语义（指针对 + 长度、`size()`/`operator[]`）与原 Slice 一致，不新增待维护类型。备选（移植 `utils::Slice`）为单处用途引入新类型，否决。

### D4: 缺失宏就地替代，不扩充 `Compiler.h`

| Filament | 处理 |
|---|---|
| `UTILS_ALWAYS_INLINE`（Field/代理成员） | 去掉：均为类内定义，天然 `inline`；无 attribute 不影响正确性 |
| `UTILS_UNROLL`（偏移计算循环） | 去掉 unroll pragma：循环短（列数级）、可被编译器自行展开 |
| `UTILS_UNUSED`（`auto UTILS_UNUSED l = {...}` 折叠占位） | `[[maybe_unused]] auto l = ...` |
| `UTILS_NOINLINE` / `UTILS_RESTRICT` / `UTILS_UNLIKELY` | 保留（`Compiler.h` 已有同名宏） |

与 allocator 移植（为语义必需而补宏）不同，此处三宏仅属性能/告警提示，就地替代零风险；不为一处用途动公共 `Compiler.h`。

### D5: 命名映射（Filament → Utils 规范）

| Filament | Utils | Filament | Utils |
|---|---|---|---|
| `getArrayCount`/`getNeededSize` | `GetArrayCount`/`GetNeededSize`（static） | `forEach` | `ForEach`（私有） |
| `data/begin/end/slice/elementAt/back`（列，模板） | `Data`/`Begin`/`End`/`Slice`/`ElementAt`/`Back`（模板，const/非 const） | `for_each`/`for_each_index` | 保持 snake（私有折叠辅助，避开 `ForEach` 重载歧义） |
| `begin/end`（行迭代器） | `Begin`/`End`（非模板重载） | `copyRange` | `CopyRange` |
| `size/capacity/setCapacity/ensureCapacity/resize/clear/swap/pop_back/push_back` | `Size`/`Capacity`/`SetCapacity`/`EnsureCapacity`/`Resize`/`Clear`/`Swap`/`PopBack`/`PushBack` | 成员 `mCapacity`… | `m_capacity`/`m_size`/`m_arrays`/`m_allocator` |
| `StructureOfArraysBase` | `SoaBase` | `Iterator`/`IteratorValue`/`IteratorValueRef`/`Field`/`Structure`/`TypeAt` | 同名保留（已是 PascalCase 名词） |

类型别名与嵌套类名已是 PascalCase，无需改名。类内声明按逻辑组分段空行、访问修饰符段 `public→private`、成员变量置底。

### D6: 注释与头文件规范

- 去掉 Apache 版权头与全部英文注释；只保留非平凡逻辑的中文注释（如"单缓冲布局下最大对齐即全体最小公倍数"、"move_each 平凡类型 memcpy 快路径"、"标量默认构造清零"），逐条按 code-style 决策树过滤
- `#pragma once`；include 顺序：本头所属 `Utils/` 组 → C++ 标准库 → C 库，组间空行（`Utils/Arena/Allocator.h`、`Utils/Compiler.h` 为第一组；`<array>/<cstddef>/<iterator>/<span>/<tuple>/<type_traits>/<utility>`、`<cassert>/<cstdint>/<cstdlib>/<cstring>` 在后）
- `assert`/`SIZE_MAX` 的 C 头：沿用 Utils 现有 `<cassert>` 风格；溢出守卫改用 `std::numeric_limits<size_t>::max()`（避免依赖 `SIZE_MAX` 的宏可见性差异）
- 移植不照搬原实现内嵌的 `// FIXME:`、`// TODO:` 等开发注释

### D7: 布局计算与搬移保持原算法

`GetOffsets(capacity)`（逐列累积 size + 对齐补齐、断言 `offset % align == 0`）、`SetCapacity`（一次 `Alloc` 整块 → `MoveEach` 逐列搬移 → `Free` 旧块）、`MoveEach` 的平凡类型 `memcpy` / 非平凡移动构造 + 析构双路径、`ResizeNoCheck` 的 `ConstructEach`/`DestroyEach` 全部保留语义，仅改名与宏替代。`ensureCapacity` 的 3/2 增长与溢出守卫保留。**与 Filament 的一处有意差异**：`ConstructEach` 对所有列执行 `new(p + i) T()` 值初始化（标量清零）——Filament 跳过平凡类型（遗留垃圾），spec Scenario「Resize 增长与收缩」要求清零，故按 spec 实现；`DestroyEach` 保持平凡析构类型跳过。另：`Iterator<SoA const*>` 的 `Get<I>()` 经 `requires` 约束仅暴露 const 版（可读），其解引用（`IteratorValueRef` 代理只存非 const 容器指针）与 Filament 相同不可编译，属继承取舍，保持不变。

### D8: 别名/伞头/测试集成

- `Soa.h` 落 `include/Utils/Soa.h`，与 `Arena/`、`string/` 同级；对外 include 路径 `Utils/Soa.h`
- `include/Utils/Utils.h` 伞头按现有分组追加 `#include "Soa.h"`（所有公共头均已聚合，保持一致）
- 头文件零 cpp、CMake 用 `GLOB_RECURSE`——新增文件零构建配置改动
- `tests/SoaTest.cpp` 由 tests 的 GLOB 自动收集，无需改 `tests/CMakeLists.txt`

## Risks / Trade-offs

- [行代理/迭代器类型多、PascalCase 批量改名易漏改] → 编译即接口契约；spec 每条 Scenario 映射到 gtest 用例逐一核对
- [`std::span` 与原 `Slice` 类型语义细节差异] → span 是标准库类型，功能超集；本库内无既有 `Slice` 使用者，无迁移面
- [释放尺寸需与分配尺寸严格一致，`SetCapacity` 处旧容量易传错] → 统一在改 `m_capacity` 之前取旧容量算 `GetNeededSize`；计数分配器单测断言 alloc/free 均衡兜底
- [去掉 `UTILS_ALWAYS_INLINE`/`UTILS_UNROLL` 可能影响热路径内联] → 类内定义 + 短循环编译器自行处理；如需强制再议补宏（本期不引入）
- [`iterator::pointer = IteratorValueRef*` 语义瑕疵（Filament FIXME）] → STL 算法不消费该 typedef（value_type/reference 足够），保持等价、不注释悬空问题

## Open Questions

- 是否需要 `SoaBase` 的分配器**实例注入**构造（如 arena 作用域后援的每帧 SoA）？本期不支持，仅默认构造策略；有需要可后续扩展
- 是否补 `3rd/Utils/doc/Soa.md` 知识库条目？沿用 Arena 先例可单独走 har-doc
