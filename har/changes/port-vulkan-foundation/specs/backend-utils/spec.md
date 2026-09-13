# Capability: backend-utils

## Purpose

后端通用设施层：为移植上游 `src/vulkan/` 提供缺失的哈希、位集、断言与范围映射原语，并固化「上游 utils 类型 → 本项目/std 等价物」的映射约定，使后续每个组件变更不再各自抉择。

上游 `src/vulkan/` 逐文件引用的 `utils/` 头文件按密度排序为：`Panic.h` 24 处、`FixedCapacityVector.h` 12 处、`compiler.h` 12 处、`Hash.h` 7 处、`bitset.h` 7 处、`debug.h` 6 处、`Mutex.h` 5 处、`CString.h` 4 处。本能力域覆盖其中**项目无对应物的部分**（新增 6 个头文件）；其余（`compiler.h` → `Utils/Compiler.h`、`Mutex.h` → `std::mutex`、`CString.h` → `NS_UTILS::String`、`FixedCapacityVector.h` → `std::vector`）以映射约定处理，不新增头文件。

**本 spec 的接口以对上游头文件逐行对照为准**，不以「上游调用点的用法」推测。命名遵循项目 `PascalCase` 规范（用户决策）——这意味着变更 4–7 移植上游调用点时须做一次机械改名，而非零改写。

## ADDED Requirements

### Requirement: Murmur 哈希函子与组合函数

`Utils/Hash.h` SHALL 提供 `NS_UTILS::hash` 命名空间下的以下设施：

- `Murmur3(uint32_t const* key, size_t wordCount, uint32_t seed) noexcept`：字对齐数据的 MurmurHash3 实现
- `MurmurSlow(uint8_t const* key, size_t byteCount, uint32_t seed) noexcept`：对任意对齐的字节序列给出与 `Murmur3` 一致的结果
- 模板函子 `MurmurHashFn<T>`：`uint32_t operator()(T const& key) const noexcept`，经 `Murmur3` 对对象原始字节求哈希；SHALL 以 `static_assert(sizeof(T) % 4 == 0)` 约束——字节数非 4 的倍数的键不可用该函子
- `Combine(size_t& seed, T const& value) noexcept`：把 `value` 的哈希混入 `seed`（boost 风格常量）
- `CombineFast(size_t& seed, T const& value) noexcept`：同上，弱化版（少一次混合运算）

**`MurmurHashFn` 是本能力域最重要的导出**——上游 7 个使用哈希容器的文件全部以 `utils::hash::MurmurHashFn<Key>` 作为哈希模板参数：

| 文件 | 键类型 | 用途 |
|---|---|---|
| `VulkanFboCache.h` | `RenderPassKey` / `FboKey` | 渲染通道与帧缓冲缓存 |
| `VulkanPipelineCache.h` | `PipelineKey` | 管线缓存 |
| `VulkanDescriptorSetLayoutCache.h` | `LayoutKey` | 布局缓存 |
| `VulkanSamplerCache.h` | `Params` | 采样器缓存 |
| `VulkanYcbcrConversionCache.h` | `Params` | YCbCr 转换缓存 |

`Combine` / `CombineFast` SHALL 提供，供自定义键类型的哈希函子在 `MurmurHashFn` 不适用（键非 4 字节对齐）时组合字段。

#### Scenario: 同一对象哈希稳定

- **WHEN** 对同一对象两次调用 `MurmurHashFn<T>{}(value)`
- **THEN** 两次返回相同的 `uint32_t`

#### Scenario: 非 4 字节对齐的键编译期拒绝

- **WHEN** 以 `sizeof(T) == 6` 的类型实例化 `MurmurHashFn<T>`
- **THEN** `static_assert` 触发编译失败

#### Scenario: 作为 unordered_map 哈希参数

- **WHEN** 声明 `std::unordered_map<K, V, NS_UTILS::hash::MurmurHashFn<K>, KEqual>`
- **THEN** 编译通过且查找语义正确

#### Scenario: 组合哈希顺序敏感

- **WHEN** 以 `Combine` 按 `(seed, a, b)` 与 `(seed, b, a)` 两种顺序组合（a ≠ b）
- **THEN** 得到不同的 seed 值

### Requirement: Bitset 模板与宽度别名

`Utils/Bitset.h` SHALL 提供模板 `NS_UTILS::Bitset<T, N = 1>`（`T` 为无符号整型）与宽度别名：

- `Bitset8` = `Bitset<uint8_t>`、`Bitset32` = `Bitset<uint32_t>`、`Bitset64` = `Bitset<uint64_t>`
- `Bitset128` = `Bitset<uint64_t, 2>`、`Bitset256` = `Bitset<uint64_t, 4>`

SHALL 提供编译期断言 `sizeof(Bitset32) == 4`、`sizeof(Bitset64) == 8`，保证存储精确可控（这正是选择自研模板而非 `std::bitset` 的理由——`VulkanDescriptorSetMask` 等类型被用作命令体成员，要求精确大小）。

方法（项目 `PascalCase` 命名；上游为 `set`/`unset`/`forEachSetBit` 等小写，移植调用点须机械改名）：

- 存取：`Set(index)` / `Set(index, bool)` / `Unset(index)` / `Flip(index)` / `Test(index) const` / `operator[](index) const`
- 整字：`GetBitsAt(n)` / `GetBitsAt(n) const` / `GetValue() const` / `SetValue(v)`（后两者仅 `N == 1` 可用）
- 批量：`Reset()` / `Clear()` / `Count() const` / `Any() const` / `None() const` / `All() const`
- 遍历：`ForEachSetBit(Fn)`——按位序升序对每个置位下标调用 `Fn(size_t index)`
- 查询：`Size() const` / `Empty() const` / `FirstSetBit() const`（无置位时返回 `size_t` 最大值）
- 位运算：`operator&=` / `|=` / `^=` / `~` / `&` / `|` / `^` / `==` / `!=`

`Set` / `Unset` / `Test` / `operator[]` 对超出位宽的下标 SHALL 在 debug 构建下经 `LOG_ASSERT` 中止；release 下不额外分支（与上游一致）。

`Count()` SHALL 以 `std::popcount` 逐字求和实现（不移植上游的 NEON 路径——本项目目标平台为 macOS/MoltenVK，arm64 下 `std::popcount` 已编译为单条指令）。

**与既有 `std::bitset` 的关系**：本头文件 SHALL NOT 替换既有 `VulkanCommandBufferPool` 中的 `std::bitset<kMaxCommandBuffers>`。两套并存，边界为「既有代码不动、新移植组件用 `Bitset*`」。

#### Scenario: 位操作往返

- **WHEN** 依次 `Set(0)`、`Set(31)`、`Unset(0)`
- **THEN** `Test(0)` 为 false、`Test(31)` 为 true、`Count()` 为 1

#### Scenario: ForEachSetBit 遍历顺序

- **WHEN** 置位下标 3、7、40 后调用 `ForEachSetBit`
- **THEN** 按 3、7、40 顺序调用回调，次数恰为 3（跨字边界正确）

#### Scenario: 存储精确

- **WHEN** 检查 `sizeof(Bitset32)` 与 `sizeof(Bitset64)`
- **THEN** 分别为 4 与 8

#### Scenario: 越界断言

- **WHEN** debug 构建下对 `Bitset32` 调用 `Set(32)`
- **THEN** `LOG_ASSERT` 触发中止

#### Scenario: FirstSetBit 无置位

- **WHEN** 对全零 `Bitset64` 调用 `FirstSetBit()`
- **THEN** 返回 `std::numeric_limits<size_t>::max()`

### Requirement: 断言宏本地等价物

`Utils/Panic.h` SHALL 以**与上游同名**的宏提供两种始终生效的断言，使上游 Vulkan 调用点零改写：

- `FILAMENT_CHECK_PRECONDITION(condition)`
- `FILAMENT_CHECK_POSTCONDITION(condition)`

二者 SHALL 支持 `<<` 流式追加诊断消息，并在条件为假时输出「宏类别 + 条件字面量 + 文件 + 行号 + 追加消息」后 `std::abort()`。二者 SHALL 在 **release 构建下仍然生效**——这些检查是 Vulkan 参数合法性守卫，release 下静默会导致未定义行为。

实现 SHALL 采用上游的条件短路手法，使条件成立时流式对象**根本不构造**（零开销）：

```cpp
#define UTILS_CHECK_CONDITION_IMPL(condition) \
    switch (0) case 0: default:
        (condition) ? (void)0 : ::utils::details::Voidify() &&

#define FILAMENT_CHECK_PRECONDITION(condition) \
    UTILS_CHECK_CONDITION_IMPL(condition) \
    ::utils::PanicStream("PRECONDITION", #condition, __FILE__, __LINE__)
```

`PanicStream` SHALL 在析构函数中输出并 `std::abort()`（临时对象在全表达式结束时析构，故 `<<` 链已累积完毕）。输出 SHALL 经既有 `utils::Log` 的 `Critical` 通道，不引入新日志设施。

#### Scenario: 条件成立不构造流对象

- **WHEN** `FILAMENT_CHECK_POSTCONDITION(result == VK_SUCCESS) << "msg"` 且条件成立
- **THEN** 走 `(void)0` 分支，`PanicStream` 不被构造，无输出、无副作用

#### Scenario: 条件失败携带消息中止

- **WHEN** 条件为假且追加了 `<< "Unable to create X. error=" << 5`
- **THEN** 诊断输出包含 `PRECONDITION`/`POSTCONDITION` 类别、条件字面量、`__FILE__`、`__LINE__` 与追加消息，随后 `abort()`

#### Scenario: release 构建仍生效

- **WHEN** `NDEBUG` 已定义时条件为假
- **THEN** 仍触发诊断与中止

### Requirement: debug 断言宏

`Utils/Debug.h` SHALL 提供 `assert_invariant(condition)`，**语义与上游 `utils/debug.h` 一致**：

- `NDEBUG` 已定义时 SHALL 展开为 `((void)0)`（**debug-only，release 下不生效**）
- 否则 SHALL 在条件为假时输出「`__func__` + 文件 + 行号 + 条件字面量」后中止

`assert_invariant` SHALL NOT 支持 `<<` 流式——上游实现直接转 `utils::panic(...)` 返回 `void`，且上游 Vulkan 代码中全部 `assert_invariant` 调用点均无流式追加（38 处 `FILAMENT_CHECK_*` 中有 9 处带 `<<`，`assert_invariant` 处为零）。

**与 `FILAMENT_CHECK_*` 的关键区别**（易错点，须在头文件注释中写明）：

| 宏 | release 下 | 流式 `<<` |
|---|---|---|
| `FILAMENT_CHECK_PRECONDITION` | 生效 | 支持 |
| `FILAMENT_CHECK_POSTCONDITION` | 生效 | 支持 |
| `assert_invariant` | **不生效** | 不支持 |

#### Scenario: debug 下条件失败中止

- **WHEN** debug 构建（`NDEBUG` 未定义）下 `assert_invariant(false)`
- **THEN** 输出函数名、文件、行号、条件字面量后中止

#### Scenario: release 下不生效

- **WHEN** `NDEBUG` 已定义时 `assert_invariant(false)`
- **THEN** 展开为 `((void)0)`，无任何副作用

#### Scenario: 不占用流式重载

- **WHEN** 编译 `assert_invariant(x)` 且未包含 `<sstream>`
- **THEN** 编译通过（`assert_invariant` 不依赖流式设施）

### Requirement: Range 半开区间

`Utils/Range.h` SHALL 提供 `NS_UTILS::Range<T>`：半开区间 `[first, last)` 的值类型。

- 成员经 `First()` / `Last()` 访问（项目规范：私有成员经 getter 暴露）
- `Size() const`：`last - first`
- `Empty() const`：`Size() == 0`
- `Contains(T const& t) const`：`first <= t && t < last`
- `Overlaps(Range<T> const& that) const`
- `const_iterator`：满足随机访问迭代器要求（`*` / `[]` / `++` / `--` / `+` / `-` / 全部比较运算）

该类型是 `RangeMap` 的依赖，上游 `RangeMap::insert` / `mergeRight` / `mergeLeft` / `findRangeT` 均操作 `Range<KeyType>`。

#### Scenario: 半开区间包含判定

- **WHEN** 构造 `Range{0, 4}`
- **THEN** `Contains(0)` 为 true、`Contains(3)` 为 true、`Contains(4)` 为 false、`Size()` 为 4、`Empty()` 为 false

#### Scenario: 重叠判定

- **WHEN** 判定 `Range{0, 4}` 与 `Range{4, 8}`
- **THEN** `Overlaps` 为 false（半开区间相邻不算重叠）

#### Scenario: 迭代器可遍历

- **WHEN** 对 `Range{2, 5}` 做范围 for
- **THEN** 依次得到 2、3、4

### Requirement: RangeMap 有序区间映射

`Utils/RangeMap.h` SHALL 提供 `NS_UTILS::RangeMap<KeyType, ValueType>`：稀疏的有序、**不重叠**区间容器，内部以 `std::map<KeyType, std::pair<Range<KeyType>, ValueType>>` 存储，区间随增删**自动分裂与合并**。

方法（项目 `PascalCase` 命名）：

- `Add(KeyType first, KeyType last, ValueType const& value)`：把 `[first, last)` 全部槽位设为 `value`，替换原有内容并按需合并相邻同值区间
- `Set(KeyType key, ValueType const& value)`：`Add` 的单元素简写
- `Has(KeyType key) const → bool`：是否存在覆盖该键的区间
- `Get(KeyType key) const → ValueType const&`：**无覆盖时经 `FILAMENT_CHECK_PRECONDITION` 中止**（不是返回默认值）
- `Clear(KeyType first, KeyType last)`：清除 `[first, last)` 内的全部元素（会裁剪跨越边界的区间）
- `Reset(KeyType key)`：`Clear` 的单元素简写
- `RangeCount() const → size_t`：内部区间对象数量（用于测试与诊断）

**`Get` 的语义是契约违反而非容错**——上游调用点依赖「查不到即 panic」来暴露布局跟踪的逻辑错误。实现 SHALL NOT 改为返回默认构造值。

#### Scenario: 区间查询命中

- **WHEN** `Add(0, 4, v1)`、`Add(4, 8, v2)` 后查询 key=5
- **THEN** `Get(5)` 返回 `v2`，`Has(5)` 为 true

#### Scenario: 相邻同值区间自动合并

- **WHEN** 依次 `Add(0, 4, v)`、`Add(4, 8, v)`（同值）
- **THEN** `RangeCount()` 为 1（两段被合并为 `[0, 8)`）

#### Scenario: 无覆盖时中止

- **WHEN** 查询未被任何区间覆盖的 key
- **THEN** `FILAMENT_CHECK_PRECONDITION` 触发中止，而非返回默认值

#### Scenario: 覆盖写入分裂既有区间

- **WHEN** `Add(0, 8, v1)` 后 `Add(2, 4, v2)`
- **THEN** `Get(1)` 为 `v1`、`Get(3)` 为 `v2`、`Get(5)` 为 `v1`，`RangeCount()` 为 3

#### Scenario: Clear 裁剪边界

- **WHEN** `Add(0, 8, v)` 后 `Clear(2, 4)`
- **THEN** `Has(3)` 为 false、`Has(1)` 为 true、`Has(5)` 为 true

该设施的主要调用方是 `VulkanTexture` 的 level 布局跟踪（变更 4），本变更只提供类型与单元测试。

### Requirement: 映射约定登记

本能力域 SHALL 登记以下映射，后续变更直接引用不得再行决策：

| 上游 | 本项目 | 处理方式 |
|---|---|---|
| `tsl::robin_map` | `std::unordered_map` | 用户决策；须逐处核对 `erase` 语义（见 design D7） |
| `utils::hash::murmur3` | `NS_UTILS::hash::Murmur3` | 仅改名 |
| `utils::hash::MurmurHashFn<T>` | `NS_UTILS::hash::MurmurHashFn<T>` | 名称已合规，直接沿用 |
| `utils::hash::combine` | `NS_UTILS::hash::Combine` | 仅改名 |
| `utils::bitset<T, N>` | `NS_UTILS::Bitset<T, N>` | 改名 + 方法名 PascalCase 化 |
| `utils::bitset32/64` | `NS_UTILS::Bitset32/64` | 仅改名 |
| `utils::RangeMap` | `NS_UTILS::RangeMap` | 仅改名 + 方法名 PascalCase 化 |
| `utils::Range` | `NS_UTILS::Range` | 改名 + 成员改 getter |
| `utils::Invocable` | `std::function` | 直接替换 |
| `utils::Condition` | `std::condition_variable` | 直接替换 |
| `utils::Mutex` | `std::mutex` | 直接替换 |
| `utils::CString` | `NS_UTILS::String` | 直接替换 |
| `utils::ImmutableCString` | `NS_UTILS::ImmutableString` | 直接替换 |
| `utils::FixedCapacityVector` | `std::vector` | 直接替换 |
| `utils::compiler.h` | `Utils/Compiler.h` | 已存在 |
| `utils::debug.h` | `Utils/Debug.h` | 本变更新增 |
| `utils::JobSystem` | `src/JobSystem.h` 空实现 | 仅线程命名/优先级两个调用点 |
| `BlueVK` | `volk` | 已存在 |
| `utils::io::ostream` | 不移植，改用 `LOG_*` | 上游调试输出专用 |
| `utils::CallStack` | 不移植 | `Panic.h` 的调用栈打印专用 |

#### Scenario: 后续变更引用约定

- **WHEN** 变更 4–7 移植任何上游文件遇到 `tsl::robin_map`
- **THEN** 直接替换为 `std::unordered_map` 并按 D7 核对 `erase` 语义，不再重新决策容器选型

#### Scenario: 命名差异有据可查

- **WHEN** 变更 4–7 遇到上游 `curMask.forEachSetBit(...)`
- **THEN** 依据本表改为 `curMask.ForEachSetBit(...)`，不重新讨论命名策略

### Requirement: 适配约束

- 新增 6 个头文件 SHALL 位于 `3rd/Utils` 子模块的 `include/Utils/`：`Hash.h` / `Bitset.h` / `Panic.h` / `Debug.h` / `Range.h` / `RangeMap.h`
- 头文件保护 SHALL 使用 `#pragma once`
- Include 顺序 SHALL 遵循 `.dsh/rules/cpp.md`：本项目 `.h` → 本项目 `""` → 第三方 → C++ 标准库 → C 库
- 命名 SHALL 遵循项目规范（**用户决策：新引入的通用容器一律用项目命名**）：类型 `PascalCase`、公有方法 `PascalCase`、私有成员 `m_camelCase`、常量 `kPascalCase`、宏 `UPPER_SNAKE_CASE`、命名空间 `snake_case`
- **两处命名特例**（须在头文件内以注释说明用途）：
  - 断言宏保留上游名 `FILAMENT_CHECK_PRECONDITION` / `FILAMENT_CHECK_POSTCONDITION` / `assert_invariant`——它们在上游调用点出现 38 次，改名会使每次对照上游都要重新翻译
  - `MurmurHashFn` 已是 `PascalCase`，直接沿用，不改名为 `MurmurHash`
- 上游的 `utils/CallStack.h` / `utils/sstream.h` 依赖 SHALL NOT 引入——`PanicStream` 以 `std::ostringstream` 实现
- `Utils/Panic.h` SHALL NOT 与既有 `src/Macro.h` 的 `EARLY_RETURN` 产生重定义冲突；本变更删除 `src/Macro.h` 中的重复定义，`EARLY_RETURN` 只保留 `Utils/Macro.h` 一份
- `Bitset` / `RangeMap` 的 `LOG_ASSERT` 越界检查 SHALL 依赖 `Utils/Log.h`；`Utils/Debug.h` 的 `assert_invariant` SHALL NOT 依赖 `Utils/Log.h`（保持与 `Utils/Log.h` 的解耦，避免 `Utils/Log.h` 被 `Debug.h` 反向包含）
- 单元测试 SHALL 落在 `3rd/Utils/tests/`，由既有 `bin/UtilsTests` 承载

#### Scenario: 编译通过

- **WHEN** 构建整个 `Backend` 静态库
- **THEN** 本能力域的全部源文件编译通过，不依赖 Filament 任何头文件

#### Scenario: 命名与风格合规

- **WHEN** 检查本能力域新增的类型与函数
- **THEN** 命名遵循项目规范（`PascalCase` 类型与公有方法、`m_camelCase` 私有成员、`kPascalCase` 常量），头文件使用 `#pragma once`，不保留上游 license / 文件头注释

#### Scenario: 不引入上游 utils 依赖

- **WHEN** 检索 6 个新增头文件的 include
- **THEN** 不出现 `utils/CallStack.h` / `utils/sstream.h` / `utils/algorithm.h` / `utils/compiler.h` 等 Filament 路径
