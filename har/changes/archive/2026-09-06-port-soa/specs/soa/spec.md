# Capability: soa

## ADDED Requirements

### Requirement: SoA 类型与单缓冲布局

提供 `template <typename Allocator, typename... Elements> class SoaBase` 与别名 `template <typename... Elements> using Soa = SoaBase<HeapAllocator, Elements...>`。容器以**单次分配**的连续缓冲存储全部 `Elements...` 列，各列按 `容量 × sizeof(E_i) + 对齐补齐` 依次排布，每列起始地址按 `max(alignof(std::max_align_t), alignof(E_i))` 对齐；`GetArrayCount()` 返回列数，`GetNeededSize(size)` 返回存储 size 个元素所需字节数。

#### Scenario: 默认堆分配

- **WHEN** 构造 `Soa<A, B, C>` 并写入数据
- **THEN** 三列数据存放于同一块堆内存（`HeapAllocator` 分配），按列指针互不重叠且各自对齐

#### Scenario: 偏移对齐

- **WHEN** 各列元素对齐要求不同（如 `alignof(E_i)` 各不相同）
- **THEN** 每列起始偏移满足 `offset % max(alignof(std::max_align_t), alignof(E_i)) == 0`，总大小恰为 `GetNeededSize(capacity)`

### Requirement: 按列访问

提供 `Data<E>()`、`Begin<E>()`、`End<E>()`（均 const/非 const 两版）、`ElementAt<E>(i)`（引用）、`Back<E>()`、`Slice<E>()`。`Slice<E>()` 返回 `std::span<TypeAt<E>>`。按列访问互不影响其他列的内容。

#### Scenario: 列读写

- **WHEN** 通过 `ElementAt<0>(i)`/`ElementAt<1>(i)` 写入两列
- **THEN** 任意列读写只作用于自身偏移区间，不破坏其他列

#### Scenario: 列区间与切片

- **WHEN** 调用 `Data<E>()`/`Begin<E>()`/`End<E>()`/`Slice<E>()`
- **THEN** 分别返回指向 E 列内存的裸指针区间与 `std::span`（长度等于 `Size()`），且与 `elementAt<E>(i)` 寻址一致

#### Scenario: 越界索引

- **WHEN** 以 `i >= Size()` 调用 `ElementAt<E>(i)`
- **THEN** 行为未定义，debug 构建提供断言拦截

### Requirement: 容量与大小管理

`Size()`/`Capacity()` 返回当前元素数与容量；`SetCapacity(c)` 重分配为至少 c（c 小于当前 `Size()` 时为 no-op，扩容时逐列搬移既有元素）；`EnsureCapacity(needed)` 不足时以 `(needed * 3 + 1) / 2` 增长（含溢出保护）；`Resize(n)` 增长时用默认构造补齐新元素、收缩时析构被丢弃元素；`Clear()` 收缩到 0。

#### Scenario: 扩容保数据

- **WHEN** 容量 4 的容器写入 4 个元素后 `SetCapacity(16)`
- **THEN** 容量变为 16，既有 4 个元素逐列搬运后值不变，新容量区间可用

#### Scenario: 缩容 no-op

- **WHEN** `Size() > 0` 时以小于 `Size()` 的容量调用 `SetCapacity`
- **THEN** 不重分配，容量与内容保持不变

#### Scenario: Resize 增长与收缩

- **WHEN** 对 `Size()=2` 的容器 `Resize(5)` 再 `Resize(3)`
- **THEN** 先默认构造补足到 5 个元素（标量清零），再析构末尾 2 个，`Size()` 变为 3，保留元素值不变

### Requirement: 元素追加/移除/行交换

提供 `PushBack()`（追加默认构造行）、`PushBack(Structure&&)`、`PushBack(Elements const&...)`、`PushBack(Elements&&...)`（展开参数逐列构造）、`PopBack()`（析构并移除末行）、`Swap(i, j)`（整行交换）。

#### Scenario: 参数展开追加

- **WHEN** 对 `Soa<uint32_t, std::string>` 调用 `PushBack(7u, std::string("x"))`
- **THEN** 追加一行：列 0 为 7，列 1 为 "x"，`Size()` 加一，超容量时自动 `EnsureCapacity`

#### Scenario: 末行移除

- **WHEN** 非空容器调用 `PopBack()`
- **THEN** 末行各列元素被析构，`Size()` 减一；空容器调用时无操作

#### Scenario: 行交换

- **WHEN** 对行 i、j 调用 `Swap(i, j)`
- **THEN** 两行的每一列内容互换，`Size()` 不变

### Requirement: 跨容器 CopyRange

提供 `CopyRange(destOffset, src, srcOffset, count)`：把源 SoA 的 `[srcOffset, srcOffset+count)` 行逐列拷贝到本容器 `[destOffset, ...)`，源与目标允许使用不同分配器（`SoaBase<OtherAllocator, Elements...>`）。

#### Scenario: 跨分配器拷贝

- **WHEN** 两个元素类型相同的 SoA（分配器不同）执行 `CopyRange`
- **THEN** 目标区间逐列得到源区间的值，源不变；越界时 debug 断言拦截

### Requirement: 行访问与结构体视图

`operator[](size_t)` 返回行引用代理 `IteratorValueRef`（可经 `Get<I>()`/`get<I>()` 访问第 I 列、可与 `Structure`/`IteratorValue` 互相赋值）；`IteratorValue` 为迭代器的 value_type（内部 `std::tuple<std::decay_t<Elements>...>`），携带全部列值，构造/赋值来自代理时逐列搬运。

#### Scenario: 行索引读写

- **WHEN** 通过 `soa[i]` 取行并用 `Get<0>()`/`Get<1>()` 读写
- **THEN** 读写落到第 i 行各列对应位置，等价于逐列 `ElementAt`

#### Scenario: 行值搬运

- **WHEN** 把 `soa[i]` 的值拷贝/移动到 `IteratorValue` 或经 `Structure` 赋值回另一行
- **THEN** 两行内容一致，源行为非平凡类型时走移动语义

### Requirement: 随机访问迭代器与 STL 算法

`Begin()`/`End()`（const/非 const）返回随机访问迭代器（`iterator`/`const_iterator`，`std::random_access_iterator_tag`），`value_type` 为 `IteratorValue`，`reference` 为 `IteratorValueRef`；支持 `std::sort` 等基于整行搬运的 STL 算法。

#### Scenario: 按字段整行排序

- **WHEN** 对行迭代器区间调用 `std::sort`（比较器用迭代器 `Get<I>()` 或 Field 取字段）
- **THEN** 各行按字段升序排列，每行各列始终作为一个整体移动

### Requirement: Field 字段引用

提供 `Field<E, IndexType>` 代理：绑定 `(SoA&, IndexType)` 后提供到 `TypeAt<E>` 的隐式转换、`operator->`、取址、赋值、比较与 `operator()(ARGS...)`（当列元素为可调用对象时透传调用）。

#### Scenario: 字段读写与比较

- **WHEN** 用 `Field<0>{soa, i}` 赋值、隐式转 `TypeAt<0>`、与 `TypeAt<0>` 比较
- **THEN** 操作均映射到 `soa.ElementAt<0>(i)`，索引经 `Field` 移动赋值时按行搬运

### Requirement: 拷贝/移动语义

`SoaBase` 禁止拷贝构造与拷贝赋值（编译期报错）；移动构造/移动赋值通过交换容量、大小、数组指针与分配器完成（noexcept）。

#### Scenario: 拷贝被禁用

- **WHEN** 拷贝构造或拷贝赋值 `SoaBase`
- **THEN** 编译期报错（`= delete`）

#### Scenario: 移动转移所有权

- **WHEN** 移动构造/移动赋值 SoA
- **THEN** 目标接管原缓冲与元素，源变为空（大小/容量为 0），无重复释放

### Requirement: 元素生命周期正确性

非平凡类型（含资源所有权）的列元素在扩容搬移、`Resize` 增长/收缩、`PopBack`、`Clear` 与析构中严格对称地构造/析构：平凡可拷贝且平凡析构的列走 `memcpy` 快路径，其余走移动构造 + 析构。

#### Scenario: 非平凡类型无泄漏

- **WHEN** 含 `std::string`/计数类型列的 SoA 反复 `PushBack` 扩容、`Resize`、`PopBack`、`Clear` 直至析构
- **THEN** 每个构造的元素恰好析构一次，无泄漏、无双重析构

### Requirement: 分配器契约

`SoaBase` 的 `Allocator` 模板参数须满足 Utils 分配策略契约（派生自 `AllocatorPolicyBase`、可默认构造），内部以值持有；`Alloc(size, alignment)` 分配整块缓冲，`Free(p, size)` 以实际大小释放。默认 `HeapAllocator` 满足该契约。

#### Scenario: 自定义计数分配器

- **WHEN** 传入记录 `Alloc`/`Free` 次数的自定义分配器策略并完成构造→扩容→析构
- **THEN** 分配的缓冲块与释放一一对应（构造 1 次 + 扩容重分配若干次，最终全部释放）
