# Capability: allocator

## Purpose

从 Filament 移植的分配器家族：`LinearAllocator`/`HeapAllocator`/`PoolAllocator`（含 WithFallback）覆盖线性、堆、对象池三类分配策略；`FreeList`/`AtomicFreeList` 提供空闲链表（原子版线程安全）；`Arena` 作为分配器门面（锁策略 + Area 内存区），`ArenaScope` 实现作用域自动回收，`STLAllocator` 适配标准库容器。`utils::pointer` 提供指针运算工具。基类 `AllocatorPolicyBase`/`AreaPolicyBase` 定义家族接口契约（纯虚交集），差异能力由 concept 按使用点约束。

## Requirements

### Requirement: Pointer 指针工具

提供 `utils::pointer` 命名空间与 `Pointer.h` 头文件，包含三个编译期友好的指针运算函数：`Add(P*, size_t)` 按字节偏移指针、`Align(P*, size_t)` 向上对齐到 2 的幂、`Align(P*, size_t, size_t)` 先加偏移再对齐。对齐函数的前置条件为 alignment 是 2 的幂且非零，debug 构建断言校验。

#### Scenario: 指针偏移

- **WHEN** 调用 `Add(p, n)`
- **THEN** 返回 `(P*)(uintptr_t(p) + n)`，保持指针类型

#### Scenario: 指针对齐

- **WHEN** 调用 `Align(p, 16)` 且 p 未对齐
- **THEN** 返回不小于 p 且 16 字节对齐的最近地址

#### Scenario: 对齐前置条件

- **WHEN** alignment 为 0 或非 2 的幂
- **THEN** debug 构建触发断言（`assert`）；release 构建行为未定义

### Requirement: AllocatorPolicy 基类与模板验证

定义基类 `AllocatorPolicyBase`，声明纯虚交集接口 `Alloc(size_t, size_t)`（两参）与 `Free(void*, size_t)` 及虚析构；所有分配器类（`LinearAllocator`、`HeapAllocator`、`LinearAllocatorWithFallback`、`PoolAllocator`、`PoolAllocatorWithFallback`）继承并实现之（3 参扩展如 extra/offset 保留为派生类重载）。`Arena` 模板通过 `requires DerivedFromAllocatorPolicy<AllocatorPolicy>` 验证传入分配器为基类派生类。家族差异能力（`Reset`/`Rewind`/`GetCurrent`）由 `Resettable`/`Rewindable`/`Currentable` 概念在对应方法调用处约束。

#### Scenario: 合法分配器

- **WHEN** 以 `LinearAllocator`、`HeapAllocator` 等内置分配器实例化 `Arena`
- **THEN** 编译通过（concept 满足）

#### Scenario: 非法分配器

- **WHEN** 以不继承 `AllocatorPolicyBase` 的类型实例化 `Arena`
- **THEN** 编译期报 constraints 错误，指出该类型不满足 `DerivedFromAllocatorPolicy`

#### Scenario: 差异能力按使用点约束

- **WHEN** 对 `Arena<HeapAllocator, ...>` 调用 `Reset()`
- **THEN** 编译期报错（`HeapAllocator` 不满足 `Resettable`），不影响其他方法

### Requirement: LinearAllocator 线性分配器

`LinearAllocator` 从内存区间线性分配：`Alloc(size, alignment = max_align_t, extra = 0)` 无分支推进写指针（先对齐再判定剩余空间，失败返回 `nullptr` 且不推进）；不可释放单块（`Free` 为空操作）；`Rewind(p)` 回退写指针到区间内任意点；`Reset()` 回退到起点；`GetCurrent()` 返回当前写指针；`Allocated()`/`Available()` 返回已用/剩余字节；支持移动构造与 `Swap`，禁止拷贝。内部以 `uint32_t` 存储偏移（区间上限 4GB），该限制在代码注释中说明。

#### Scenario: 顺序分配

- **WHEN** 在 1KB 区间上连续分配若干块
- **THEN** 各块地址单调递增、互不重叠，`Allocated()` 为已分配总字节数

#### Scenario: 空间不足

- **WHEN** 请求分配大于剩余空间的块
- **THEN** 返回 `nullptr`，写指针不推进，后续分配不受影响

#### Scenario: 回退与重置

- **WHEN** 调用 `Rewind(p)` 或 `Reset()`
- **THEN** 写指针回到指定点/起点，`Available()` 相应恢复

### Requirement: HeapAllocator 堆分配器

`HeapAllocator` 每次 `Alloc(size, alignment)` 走对齐堆分配（`posix_memalign`/Windows `_aligned_malloc`），`Free(p)` 对应释放；`Swap` 为空操作。分配器无状态、可默认构造。仅实现两参 `Alloc`/`Free` 交集（无 `Reset`/`Rewind`/`GetCurrent`）。

#### Scenario: 堆分配与释放

- **WHEN** 调用 `Alloc(size, alignment)` 后 `Free(p)`
- **THEN** 返回的指针满足对齐要求，释放无泄漏（构造析构生命周期正确）

### Requirement: LinearAllocatorWithFallback 线性 + 堆兜底

`LinearAllocatorWithFallback` 组合线性分配器与堆分配器：线性区分配失败时从堆分配并记录堆块；`Reset()` 释放全部堆块并回退线性区；析构时隐式 `Reset()`；`IsHeapAllocation(p)` 判定指针是否落在线性区之外（即堆块）。

#### Scenario: 线性区耗尽回退堆

- **WHEN** 线性区空间不足
- **THEN** 分配从堆成功返回，`IsHeapAllocation(p)` 为 true，`Reset()` 后堆块被释放

### Requirement: FreeList 空闲链表

`FreeList` 以元素为单位初始化一段内存区间（`FreeList(begin, end, elementSize, alignment, extra)`，链表节点预埋在块内），`Pop()` 取出头节点、`Push(p)` 将块压回头部（placement-new 管理节点生命周期），`GetFirst()` 返回链表头。debug 构建校验 Push/Pop 的指针落在初始化区间内。

#### Scenario: 弹出与压回

- **WHEN** 依次 `Pop()` 全部块后再 `Push` 回
- **THEN** 弹出的指针集合与压回顺序构成 LIFO 语义，块无重叠

### Requirement: AtomicFreeList 线程安全空闲链表

`AtomicFreeList` 提供与 `FreeList` 相同的 Pop/Push 语义，但以 tagged 指针（32 位 offset + 32 位 tag 组成 8 字节原子 `HeadPtr`）实现无锁并发：`Pop` 用 acquire 语义 CAS，`Push` 用 release 语义 CAS，tag 防 ABA。多线程并发 Pop/Push 不丢失节点、不破坏链表。

#### Scenario: 并发弹出与压回

- **WHEN** 多个线程并发 `Pop()`/`Push()` 同一 AtomicFreeList
- **THEN** 每个块恰好被弹出一次，链表结构不被破坏（TSAN 无数据竞争误报）

### Requirement: PoolAllocator 对象池

`PoolAllocator<ELEMENT_SIZE, ALIGNMENT, OFFSET, FREELIST>` 基于空闲链表实现固定大小对象池：`Alloc(size = ELEMENT_SIZE, alignment = ALIGNMENT, offset = OFFSET)` 断言调用参数不超模板参数并弹出空闲块；`Free(p)` 压回；`GetSize()` 返回元素大小；`GetCurrent()` 返回链表头。`ELEMENT_SIZE` 须容纳 `FREELIST::Node`（`static_assert`）。模板别名 `ObjectPoolAllocator<T>` / `ThreadSafeObjectPoolAllocator<T>` 分别以 `FreeList` / `AtomicFreeList` 实例化。

#### Scenario: 池化分配释放

- **WHEN** 从池中 `Alloc()` 若干对象后 `Free()` 全部
- **THEN** 重复分配返回相同地址集合（复用），无新内存消耗

#### Scenario: 参数超限

- **WHEN** `Alloc` 传入的 size/alignment/offset 超过模板参数
- **THEN** debug 构建触发断言；release 构建行为未定义

### Requirement: PoolAllocatorWithFallback 池 + 堆兜底

`PoolAllocatorWithFallback` 组合对象池与堆分配器：池空时从堆分配，`Free` 按 `IsHeapAllocation(p)` 分流到池或堆。

#### Scenario: 池耗尽回退堆

- **WHEN** 池中空闲块耗尽
- **THEN** 分配从堆成功返回，`IsHeapAllocation(p)` 为 true，`Free` 正确归还堆

### Requirement: AreaPolicy 基类与 Area 实现

定义基类 `AreaPolicyBase`，声明纯虚 `GetData()`/`GetSize()` 及虚析构，**不存在 `AreaPolicy` 命名空间**。三个 Area 类继承之并上提为平级类：
- `StaticArea`：持有外部提供的 `[begin, end)` 区间，不拥有内存，可拷贝/移动/`Swap`，提供 `GetData()`/`GetBegin()`/`GetEnd()`/`GetSize()`
- `HeapArea`：构造时 `malloc(size)` 拥有内存，析构 `free`，禁止拷贝/移动，提供同样查询接口
- `NullArea`：零大小占位，`GetData()` 返回 `nullptr`、`GetSize()` 返回 0

#### Scenario: 外部区间

- **WHEN** 以栈/静态缓冲构造 `StaticArea(begin, end)`
- **THEN** `GetData()/GetBegin()/GetEnd()/GetSize()` 返回传入区间，析构不释放外部内存

#### Scenario: 堆拥有区间

- **WHEN** 构造 `HeapArea(size)`
- **THEN** 分配 `size` 字节且 `GetSize()` 为 `size`，析构时内存归还系统

#### Scenario: 模板验证

- **WHEN** 以不继承 `AreaPolicyBase` 的类型实例化 `Arena`
- **THEN** 编译期报 constraints 错误

### Requirement: LockingPolicy 锁策略

`LockingPolicy` 命名空间提供 `NoLock`（lock/unlock 空操作，保持小写以符合 `std::lock_guard` 的 Lockable 概念）与 `Mutex`（`using Mutex = std::mutex`）两种锁策略，供 `Arena` 模板使用。`NoLock` 为进程内单线程场景零开销；`Mutex` 配合 `std::lock_guard` 提供互斥。

#### Scenario: 无锁策略

- **WHEN** 以 `NoLock` 实例化 `Arena` 且单线程使用
- **THEN** 分配路径无任何同步开销

#### Scenario: 互斥策略

- **WHEN** 以 `Mutex` 实例化 `Arena` 且多线程并发 `Alloc`/`Free`
- **THEN** 每次操作互斥执行，无数据竞争

### Requirement: Arena 分配器门面

`Arena<AllocatorPolicy, LockingPolicy, AreaPolicy = HeapArea>` 是分配器门面：
- 模板参数经 `requires DerivedFromAllocatorPolicy` / `DerivedFromAreaPolicy` 验证为基类派生类
- 构造 `Arena(size, args...)`（无 name 参数）或 `Arena(AreaPolicy&&, args...)`，转发参数给 Area 与分配器
- `Alloc(size, alignment = max_align_t)` 及带 `extra` 的重载、`Alloc<T>(count, ...)`（仅平凡析构类型，`is_trivially_destructible_v` 约束 + 乘法溢出防护返回 `nullptr`）
- `Free(p)` / `Free(p, size)`（`p == nullptr` 时安全跳过）
- `Reset()` / `Rewind(addr)` / `GetCurrent()`（分别要求分配器满足 `Resettable`/`Rewindable`/`Currentable`）
- `Make<T>(args...)` 分配并就地构造；`Destroy<T>(p)` 析构并释放
- `GetAllocator()` / `GetArea()` 访问器
- **不提供**：TrackingPolicy 相关的一切（listener 构造、`getListener`/`setListener`/`emplaceListener`）与 `getName()`
- 禁止拷贝，支持 `Swap`

#### Scenario: 基本分配释放

- **WHEN** `Arena<HeapAllocator, NoLock>` 上 `Alloc`/`Free`
- **THEN** 分配满足对齐，释放安全，生命周期内无泄漏

#### Scenario: 对象构造与析构

- **WHEN** `Make<T>(...)` 创建对象后 `Destroy<T>(p)`
- **THEN** 构造/析构被正确调用，内存归还

#### Scenario: 乘法溢出防护

- **WHEN** `Alloc<T>(count, ...)` 且 `count * sizeof(T)` 溢出
- **THEN** 返回 `nullptr`，不产生异常

#### Scenario: 平凡析构约束

- **WHEN** 对非平凡析构类型调用 `Alloc<T>(count, ...)`
- **THEN** 编译期报错（SFINAE 禁用）

### Requirement: HeapArena 别名

提供 `HeapArena` 别名：`Arena<HeapAllocator, LockingPolicy::NoLock>`（默认 AreaPolicy 为 `HeapArea`）。

#### Scenario: 别名使用

- **WHEN** 实例化 `HeapArena(size)`
- **THEN** 等价于 `Arena<HeapAllocator, NoLock, HeapArea>(size)`

### Requirement: ArenaScope 作用域回收

`ArenaScope<ARENA>` 持有分配器引用：构造时记录当前写指针（`GetCurrent()`），析构时先运行 finalizer 链（`Make<T>` 对非平凡析构类型记录析构回调，逆序执行）再 `Rewind` 回记录点。`Make<T>` 对平凡析构类型直接分配（零 finalizer 开销）。类不可拷贝、不可移动。

#### Scenario: 作用域回退

- **WHEN** 在 `ArenaScope` 生命周期内 `Make` 若干对象后离开作用域
- **THEN** 非平凡析构对象按逆序析构，分配内存回退到作用域起点

#### Scenario: 平凡类型零开销

- **WHEN** `Make<T>` 且 T 为平凡析构类型
- **THEN** 直接走 `Arena::Make`，不创建 finalizer 节点

### Requirement: STLAllocator 标准库适配

`STLAllocator<TYPE, ARENA>` 实现 C++ 标准分配器接口（`value_type`/`rebind`/`propagate_on_container_move_assignment`/`is_always_equal`），`allocate(n)` 走 `Arena::Alloc`、`deallocate(p, n)` 走 `Arena::Free`，相等性按所持 Arena 引用地址判定。`allocate`/`deallocate` 保持标准库要求的原名。`ARENA` 需支持 `Alloc`/`Free` 语义。

#### Scenario: 容器适配

- **WHEN** 以 `STLAllocator<T, Arena>` 构造 `std::vector<T>`
- **THEN** 元素内存全部来自所持 Arena，容器析构后 Arena 可整体 `Reset()`

#### Scenario: 相等性

- **WHEN** 比较两个指向同一 Arena 与不同 Arena 的分配器
- **THEN** 前者相等、后者不相等（按 `std::addressof(mArena)` 判定）

### Requirement: 对齐内存分配工具

`mem/MemAlign.h` 提供跨平台对齐分配自由函数 `AlignedAlloc(size, alignment)` / `AlignedFree(p)`：POSIX 走 `posix_memalign`，Windows 走 `_aligned_malloc`/`_aligned_free`；`AlignedAlloc` 前置条件为 alignment 是 2 的幂且为 `sizeof(void*)` 的倍数（debug 断言）。

#### Scenario: 平台分配

- **WHEN** 调用 `AlignedAlloc(size, 64)`
- **THEN** 返回 64 字节对齐的内存，`AlignedFree` 正确释放
