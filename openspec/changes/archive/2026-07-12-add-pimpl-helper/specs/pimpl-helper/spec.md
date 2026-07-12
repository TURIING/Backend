## ADDED Requirements

### Requirement: Pimpl helper template class

Utils 库 SHALL 提供 `utils::Impl<T>` 模板类，封装 PIMPL 惯用法中的堆分配、生命周期管理和值语义。

#### Scenario: Default construction
- **WHEN** 以 `utils::Impl<Foo> impl;` 声明对象
- **THEN** `m_pImpl` 指向一个通过 `new Foo` 构造的 `Foo` 实例

#### Scenario: Parameter forwarding construction
- **WHEN** 以 `utils::Impl<Foo> impl(arg1, arg2);` 声明对象
- **THEN** `m_pImpl` 指向一个通过 `new Foo(std::forward<Args>(args)...)` 构造的 `Foo` 实例

#### Scenario: Destruction releases ownership
- **WHEN** `utils::Impl<Foo>` 对象析构
- **THEN** `delete m_pImpl` 被调用，释放底层 `Foo` 实例

#### Scenario: Copy construction performs deep copy
- **WHEN** 对 `utils::Impl<Foo>` 执行拷贝构造，且 `Foo` 可拷贝
- **THEN** 新对象的 `m_pImpl` 指向 `new Foo(*rhs.m_pImpl)`，两者独立拥有各自的 `Foo` 实例

#### Scenario: Copy assignment performs deep copy
- **WHEN** 对 `utils::Impl<Foo>` 执行拷贝赋值，且 `Foo` 可拷贝
- **THEN** `*m_pImpl = *rhs.m_pImpl` 被调用，左侧对象的底层 `Foo` 获得与右侧相同的值

#### Scenario: Move construction transfers ownership
- **WHEN** 对 `utils::Impl<Foo>` 执行移动构造
- **THEN** 新对象的 `m_pImpl` 指向原对象的 `m_pImpl`，原对象的 `m_pImpl` 变为 `nullptr`

#### Scenario: Move assignment swaps ownership
- **WHEN** 对 `utils::Impl<Foo>` 执行移动赋值
- **THEN** 两侧 `m_pImpl` 指针交换，原对象的 `m_pImpl` 最终指向被转移出的指针

### Requirement: Utils namespace

Utils 库的所有公开 API SHALL 位于 `utils` 命名空间下。

#### Scenario: Impl is in utils namespace
- **WHEN** 包含 `Utils/Impl.h`
- **THEN** `utils::Impl<T>` 可用，编译器可正确解析

### Requirement: Single header file

`utils::Impl<T>` 的声明与实现 SHALL 合并于单个头文件 `Impl.h` 中。

#### Scenario: Include single header
- **WHEN** 使用者执行 `#include <Utils/Impl.h>`
- **THEN** `utils::Impl<T>` 的完整声明和实现均可见，模板可正常实例化
