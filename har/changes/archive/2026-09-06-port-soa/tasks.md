# Tasks: Port SoA from Filament

## 1. 头文件骨架与内存布局

- [x] 1.1 新建 `include/Utils/Soa.h`：`SoaBase<Allocator, Elements...>` 模板声明（`requires AllocatorPolicyDerivable<Allocator> && std::default_initializable<Allocator>`）+ 类型区（`SoA`/`Structure`/`TypeAt`/`GetArrayCount`/`GetNeededSize`、`iterator`/`const_iterator` 等 using 与 `kArrayCount`）、私有成员 `m_capacity`/`m_size`/`m_arrays`/`m_allocator` 置底；include 集按规范分组（`Utils/Arena/Allocator.h`、`Utils/Compiler.h` → `<array>/<cstddef>/<iterator>/<span>/<tuple>/<type_traits>/<utility>` → `<cassert>/<cstdint>/<cstdlib>/<cstring>`）
- [x] 1.2 偏移与布局计算：私有 static `GetOffsets(capacity)`（逐列 size+补齐、断言对齐）与 `GetOffset`；`GetNeededSize` 用 `std::numeric_limits` 溢出相关写法核对
- [x] 1.3 容量/生命周期公有接口：默认构造、`explicit SoaBase(size_t capacity)`、`SetCapacity`（一次 Alloc → `MoveEach` → `Free(旧块, 旧容量尺寸)`，缩容 no-op）、`EnsureCapacity`（3/2 增长 + 溢出守卫）、`Resize`、`Clear`、`Size`/`Capacity`
- [x] 1.4 移动构造/移动赋值（成员 `std::swap`）与析构（`DestroyEach` + `Free(基址, GetNeededSize(m_capacity))`）；拷贝构造/赋值 `= delete`
- [x] 1.5 列访问接口：`Data`/`Begin`/`End`/`ElementAt`/`Back`（模板、const/非 const）与 `Slice`（返回 `std::span`）；私有工具 `ForEach`/`for_each`/`for_each_index`（折叠辅助保留 snake 命名）

## 2. 元素生命周期内部实现

- [x] 2.1 `ResizeNoCheck`/`ConstructEach`/`DestroyEach`：增长默认构造（标量清零、非平凡类型 `new(p+i) T()`）、收缩逐元素析构，平凡类型跳过
- [x] 2.2 `MoveEach`：重分配时平凡可拷贝+平凡析构走 `memcpy` 快路径，其余逐元素移动构造 + 析构旧元素；搬移后更新 `m_arrays` 各列指针（`reinterpret_cast` + 偏移，restrict 保留）

## 3. 行操作与代理类型

- [x] 3.1 `PushBack` 三档重载（无参/`Structure&&`/展开 `Elements`）与 `push_back_unsafe` 折叠实现（`BuildElementIndices` 索引序列）、`PopBack`、`Swap(i, j)`
- [x] 3.2 `CopyRange`（跨分配器源）与私有 `Copier`
- [x] 3.3 `IteratorValueRef` 行引用代理：`Get<I>` 访问、四组 `Assign`（值/结构体，const/&&）、与 `IteratorValue` 互赋、`swap` 好友
- [x] 3.4 `IteratorValue` 行值（内部 `std::tuple<std::decay_t<Elements>...>`）：从 `IteratorValueRef` 拷贝/移动初始化、默认五特殊成员
- [x] 3.5 `Iterator` 随机访问迭代器（`random_access_iterator_tag`、`operator*`/`[]`/`Get<I>`/算术/比较/前后缀 ++--）与行 `Begin`/`End`
- [x] 3.6 `Field<E, IndexType>` 字段引用：隐式转换、`operator->`/取址/赋值/比较/`operator()` 透传，均映射 `ElementAt`

## 4. 命名/风格收口与集成

- [x] 4.1 全头自查：方法 PascalCase、私有辅助区分、成员 `m_` 前缀、`noexcept`/`[[nodiscard]]` 标注核对（类型别名/静态查询不丢 nodiscard）
- [x] 4.2 注释收口：去版权头与英文注释；只保留非平凡逻辑的中文注释（决策树过滤，单函数 ≤3 条）；类内声明按逻辑组空行分段
- [x] 4.3 宏替代核对：`UTILS_ALWAYS_INLINE`/`UTILS_UNROLL` 删除、`UTILS_UNUSED` 占位改 `[[maybe_unused]]`，确认 `UTILS_NOINLINE`/`UTILS_RESTRICT`/`UTILS_UNLIKELY` 保留引用无误
- [x] 4.4 `include/Utils/Utils.h` 伞头按分组追加 `#include "Soa.h"`
- [x] 4.5 编译验证：`3rd/Utils` 下 `cmake --build build` 通过，无告警新增

## 5. 测试

- [x] 5.1 新建 `tests/SoaTest.cpp`（gtest），用例覆盖 spec 全部 Scenario：布局对齐与单缓冲（含混合对齐列）；列访问/切片（span）；`SetCapacity` 扩容保数据、缩容 no-op、`Resize` 增长/收缩、`Clear`；`PushBack` 三档（含 `std::string` 列）/`PopBack`/`Swap`；`CopyRange`（含跨分配器）；行索引读写与 `Get<I>`、行值搬运；`std::sort` 按字段整行排序（迭代器 + Field）；移动语义转移所有权；非平凡类型计数类验证构造/析构对称（扩容、Resize、Clear、析构全程无泄漏/无双重析构）；自定义计数分配器断言 `Alloc`/`Free` 均衡
- [x] 5.2 运行 `UtilsTests`（`ctest` 或直接运行测试目标）全部通过
- [x] 5.3 spec Scenario ↔ 用例对照核对：无遗漏需求

## 6. 收尾

- [x] 6.1 通读 `Soa.h` 复核与 Filament 语义等价（布局、搬移、代理语义）；确认无悬浮注释/临时代码
- [x] 6.2 归档准备说明（apply 完成后由 har-archive 处理）：将 soa 能力域 spec 同步至 `har/specs/soa/spec.md` 并登记变更完成
