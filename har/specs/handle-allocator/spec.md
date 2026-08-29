# Capability: handle-allocator

## Purpose

句柄分配与生命周期管理：`HandleAllocator<P0, P1, P2>` 基于 `utils::Arena` + 三层 `PoolAllocator` 池化分配句柄对象内存，句柄 id 内嵌 age 用于 use-after-free 检测，arena 耗尽时回退系统堆（慢路径）；`DebugTag` 基类为句柄关联调试标签。作为 handle 域（句柄类型）与 allocator 域（底层分配器）之间的句柄生产/消费层，供 Driver 后端（当前为 Vulkan）使用。

## Requirements

### Requirement: 句柄分配与构造

`HandleAllocator` SHALL 提供 `allocateAndConstruct<D>(args...)`（分配并就地构造，返回 `Handle<D>`）、`allocate<D>()`（仅分配不构造）、`destroyAndConstruct<D, B>(handle, args...)`（析构后原地重建，`std::is_base_of_v<B, D>` 约束）、`construct<D, B>(handle, args...)`（在既有句柄处构造）、`deallocate<B, D>(handle, p)` / `deallocate<D, B>(handle)`（析构并归还，支持 `p == nullptr` 安全跳过）。所有构造/析构使用 placement-new 与显式析构调用。

#### Scenario: 分配并构造

- **WHEN** 对派生对象调用 `allocateAndConstruct<D>(args...)`
- **THEN** 返回持有有效 id 的 `Handle<D>`，`handle_cast<D*>(h)` 指向的对象已以 args 构造

#### Scenario: 原地重建

- **WHEN** 对已分配句柄调用 `destroyAndConstruct<D>(handle, args...)`
- **THEN** 旧对象被析构，同一地址以 args 重新构造，handle id 不变

#### Scenario: 释放

- **WHEN** `deallocate(handle, p)` 且 p 非空
- **THEN** 对象析构、内存归还池；p 为空时直接返回

### Requirement: 三层池化分桶

`HandleAllocator<P0, P1, P2>` SHALL 按 `sizeof(D)` 在编译期选择桶：`sizeof(D) <= P0` 用桶 0，`<= P1` 用桶 1，`static_assert(sizeof(D) <= P2)` 后用桶 2；每个桶为 `utils::PoolAllocator<SIZE, alignof(std::max_align_t), sizeof(Node)>`（Node 为 1 字节 age 结构），三桶均分 arena 内存区。池化分配路径 SHALL 为 `UTILS_NOINLINE` 且每次调用只生成对应桶的特化版本。

#### Scenario: 按大小分桶

- **WHEN** 分配 `sizeof(D) <= P0`、`P0 < sizeof(D) <= P1`、`P1 < sizeof(D) <= P2` 三种对象
- **THEN** 分别从桶 0/1/2 分配，桶间不串用

#### Scenario: 超出最大桶

- **WHEN** `sizeof(D) > P2` 实例化分配路径
- **THEN** 编译期 `static_assert` 失败

#### Scenario: 池复用

- **WHEN** 释放后再次分配同大小对象
- **THEN** 优先复用释放块，无新增内存消耗

### Requirement: age 机制与 use-after-free 检测

句柄 id SHALL 将 age 编码于 `HANDLE_AGE_SHIFT`（27）起的 4 位，`HANDLE_INDEX_MASK`（0x07FFFFFF）为池索引，`HANDLE_HEAP_FLAG`（0x80000000）标记堆（慢路径）句柄。age SHALL 存储于池块 `OFFSET=sizeof(Node)` 预留区（`pNode[-1].age`），arena 初始化时整区清零；分配时读取该 age 写入句柄，释放时校验句柄 age 与内存 age 一致（不一致即 double-free / use-after-free）后按 `(age + 1) & 0xF` 递增。检测可通过构造参数 `disableUseAfterFreeCheck` 关闭。

#### Scenario: 释放后复用

- **WHEN** 对象 A 释放后同一块被分配给对象 B
- **THEN** B 的句柄 age 不同于 A 的句柄 age

#### Scenario: 检测 use-after-free

- **WHEN** 以已释放句柄（旧 age）调用 `handle_cast` 或 `is_valid`
- **THEN** debug 构建下 `handle_cast` 触发致命日志（`LOG_CRITICAL`，abort）；`is_valid` 返回 false

#### Scenario: 检测 double-free

- **WHEN** 对同一句柄连续两次 `deallocate`
- **THEN** 第二次释放触发致命日志（`LOG_CRITICAL`，abort）

#### Scenario: 关闭检测

- **WHEN** 以 `disableUseAfterFreeCheck = true` 构造
- **THEN** age 校验与致命日志全部跳过，仅保留递增

### Requirement: 慢路径溢出分配

arena 池耗尽时，`allocateHandleSlow` SHALL 走 `::malloc` 并以 `std::atomic<HandleBase::HandleId>` 递增分配新 id（置 `HANDLE_HEAP_FLAG`）；id 达到 `HANDLE_HEAP_FLAG` 上限时 SHALL 触发致命日志。堆句柄 SHALL 经 `mOverflowMap`（`std::unordered_map<HandleId, void*>`，`utils::Mutex` 保护）登记/查询/移除，析构时若映射非空 SHALL 输出泄漏日志并 `::free` 剩余内存。

#### Scenario: 池耗尽回退

- **WHEN** 池中无空闲块
- **THEN** 分配从系统堆成功返回，句柄置堆标志，`handle_cast` 经映射正确解引用

#### Scenario: 堆句柄释放

- **WHEN** 释放堆句柄
- **THEN** 从映射移除并 `::free`，映射最终为空

#### Scenario: 泄漏提示

- **WHEN** 析构时映射非空
- **THEN** 输出未释放句柄的警告日志并释放剩余内存

### Requirement: 句柄转换与有效性

`handle_cast<Dp, B>(handle)` SHALL 经 id 解出指针并 `static_cast`（`std::is_pointer_v<Dp>` 且 `std::is_base_of_v<B, remove_pointer_t<Dp>>` 约束），null 句柄先断言拦截；池句柄按 `(id & HANDLE_INDEX_MASK) * alignof(std::max_align_t)` 偏移 arena 基址解指针（快路径，内联）。`is_valid(handle)` SHALL 对池句柄比较 age、对堆句柄判空。

#### Scenario: 快路径解指针

- **WHEN** `handle_cast` 池句柄
- **THEN** 以基址 + 索引 × 对齐直接得到对象指针，无映射查找

#### Scenario: 空句柄拦截

- **WHEN** 对空句柄调用 `handle_cast`
- **THEN** debug 构建触发 `LOG_ASSERT`

#### Scenario: 类型约束

- **WHEN** `Dp` 非指针或 `B` 非 `D` 的基类
- **THEN** 编译期 SFINAE 禁用该重载

### Requirement: 调试标签

`DebugTag` SHALL 以 `utils::Mutex` + `std::unordered_map<HandleId, utils::ImmutableString>` 维护句柄标签，构造时 `reserve(512)`；`associateTagToHandle` SHALL 为池句柄截断 age 位段后关联（复用后标签随之回收）、为堆句柄关联（`disableHeapHandleTags` 时跳过）；`getHandleTag` SHALL 返回关联标签，无标签时返回 `"(no tag)"`；查找/写入 SHALL 持锁。

#### Scenario: 标签关联与查询

- **WHEN** 关联标签后查询同一句柄
- **THEN** 返回对应标签；未关联句柄返回 `"(no tag)"`

#### Scenario: 堆标签禁用

- **WHEN** 以 `disableHeapHandleTags = true` 构造且为堆句柄关联标签
- **THEN** 标签被忽略，映射不变

### Requirement: 适配约束

`HandleAllocator` SHALL 位于 `Backend` 命名空间（`BEGIN_NS_BACKEND`），复用本项目 `Backend/Handle.h` 的 `HandleBase::HandleId`。内嵌 `Allocator` 策略类 SHALL 继承 `utils::AllocatorPolicyBase`（满足 `Arena` 的 `AllocatorPolicyDerivable` 约束），经 `Arena(size, flag)` 构造（本项目 Arena 无 name 参数、无 TrackingPolicy）。age 校验与递增 SHALL 在 HandleAllocator 层完成（Arena 不传递 age）；`Free(p, size)` SHALL 只做池归还。`tsl::robin_map` SHALL 以 `std::unordered_map` 替代。断言/日志 SHALL 映射为：`assert_invariant` → `LOG_ASSERT`，`FILAMENT_CHECK_POSTCONDITION << msg` → 条件失败时 `LOG_CRITICAL`（含诊断信息，abort），`PANIC_LOG` → `LOG_ERROR`，`LOG(WARNING) <<` → `LOG_WARN`。`utils::ImmutableCString` SHALL 映射为 `utils::ImmutableString`。注释 SHALL 按项目规范重写（不保留 Filament license/文件头注释）。

#### Scenario: 编译通过

- **WHEN** 包含 `HandleAllocator.h` 并实例化 `HandleAllocatorVK`
- **THEN** 编译通过，不依赖 Filament 任何头文件与 `tsl` 库

#### Scenario: 头文件保护与包含

- **WHEN** 检查头文件
- **THEN** 使用 `#pragma once`；include 顺序符合项目规范（本项目头 → 第三方 Utils → 标准库 → C 库）

### Requirement: 显式实例化

`HandleAllocator.cpp` SHALL 以 `using HandleAllocatorVK = HandleAllocator<64, 160, 312>;` 定义 Vulkan 别名（头文件）并显式实例化 `template class HandleAllocator<64, 160, 312>;`（cpp）。不移植 GL/MTL/WGPU 实例。

#### Scenario: Vulkan 实例可用

- **WHEN** 链接使用 `HandleAllocatorVK` 的翻译单元
- **THEN** 非模板成员符号由显式实例化提供，链接成功

#### Scenario: 其余后端不实例化

- **WHEN** 检查编译产物
- **THEN** 仅存在 `HandleAllocator<64, 160, 312>` 的实例化
