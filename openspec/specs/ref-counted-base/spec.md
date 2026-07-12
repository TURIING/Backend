# ref-counted-base

## Purpose

提供 `utils::Ref` 侵入式引用计数基类。派生类继承后即可获得原子引用计数和自销毁能力，配合 `utils::SharedPtr<T>` 实现共享所有权。

## Requirements

### Requirement: Ref 基类提供引用计数

`utils::Ref` 类 SHALL 提供原子引用计数能力。对象构造时计数初始化为 0，通过 `AddRef()` 递增，`SubRef()` 递减；当计数由 1 减至 0 时，`SubRef()` SHALL 调用 `delete this` 销毁对象。

#### Scenario: 初始引用计数为 0

- **WHEN** 构造一个 `Ref` 派生类对象
- **THEN** `GetRefCount()` 返回 0

#### Scenario: AddRef 递增计数

- **WHEN** 调用 `AddRef()` 一次
- **THEN** `GetRefCount()` 返回值比调用前增加 1

#### Scenario: SubRef 递减计数

- **WHEN** 当前计数为 N (N > 0)，调用 `SubRef()` 一次
- **THEN** `GetRefCount()` 返回 N - 1

#### Scenario: SubRef 归零时销毁对象

- **WHEN** 当前计数为 1，调用 `SubRef()`
- **THEN** 对象被销毁（`delete this` 被调用，派生类析构函数执行）

#### Scenario: 禁止拷贝

- **WHEN** 尝试拷贝构造或拷贝赋值 `Ref` 对象
- **THEN** 编译失败（拷贝构造和拷贝赋值已 `= delete`）

#### Scenario: 禁止移动

- **WHEN** 尝试移动构造或移动赋值 `Ref` 对象
- **THEN** 编译失败（移动构造和移动赋值已 `= delete`）

### Requirement: Ref 多线程安全

`Ref` 的 `SubRef()` 方法 SHALL 使用 `memory_order_acq_rel` 保证多线程环境下的内存可见性。多线程并发 `AddRef`/`SubRef` 时，对象 SHALL 恰被销毁一次。

#### Scenario: 多线程并发 AddRef 和 SubRef

- **WHEN** N 个线程各自对同一对象执行 AddRef 后立即 SubRef
- **THEN** 对象恰被销毁一次，无 double-free 或 use-after-free

#### Scenario: 跨线程传递 SharedPtr 后对象状态可见

- **WHEN** 线程 A 创建 SharedPtr 并修改对象字段，将 SharedPtr 传递给线程 B
- **THEN** 线程 B 通过 SharedPtr 读到的对象字段为线程 A 写入的最新值

### Requirement: Ref 派生类的虚析构

`Ref` 类 SHALL 声明 `virtual` 析构函数，确保 `SubRef()` 中 `delete this` 时正确调用派生类析构。

#### Scenario: delete this 调用派生类析构

- **WHEN** `SubRef()` 导致 `delete this` 执行
- **THEN** 派生类的析构函数被正确调用，且 `Ref` 基类析构函数也被调用
