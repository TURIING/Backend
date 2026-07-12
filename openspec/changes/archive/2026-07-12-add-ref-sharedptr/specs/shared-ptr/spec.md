## ADDED Requirements

### Requirement: SharedPtr 默认构造为空指针

`utils::SharedPtr<T>` SHALL 支持默认构造，构造后内部指针为空。

#### Scenario: 默认构造后为空

- **WHEN** 使用默认构造函数创建 `SharedPtr<T>`
- **THEN** `Get()` 返回 `nullptr`，`operator bool` 返回 `false`

### Requirement: SharedPtr 从裸指针接管

`utils::SharedPtr<T>` SHALL 支持从 `T*` 裸指针显式构造，构造时调用 `ptr->AddRef()`。

#### Scenario: 从裸指针构造

- **WHEN** 使用 `SharedPtr<T> ptr(obj)` 从非空裸指针 `obj` 构造
- **THEN** `Get()` 返回 `obj`，`obj->GetRefCount()` 为 1

#### Scenario: 从 nullptr 构造

- **WHEN** 使用 `SharedPtr<T> ptr(nullptr)` 构造
- **THEN** `Get()` 返回 `nullptr`，不调用 `AddRef()`

### Requirement: SharedPtr 拷贝语义

`utils::SharedPtr<T>` SHALL 支持拷贝构造和拷贝赋值，拷贝时调用 `AddRef()` 增加引用计数。

#### Scenario: 拷贝构造

- **WHEN** 拷贝构造一个新的 `SharedPtr<T>`，源对象管理一个非空指针
- **THEN** 新对象指向同一个对象，引用计数增加 1

#### Scenario: 拷贝赋值

- **WHEN** 将一个 `SharedPtr<T>` 拷贝赋值给另一个 `SharedPtr<T>`
- **THEN** 目标对象先 `SubRef` 旧指针，再指向新指针并 `AddRef()`

### Requirement: SharedPtr 移动语义

`utils::SharedPtr<T>` SHALL 支持移动构造和移动赋值，移动后源对象内部指针为空，引用计数不变。

#### Scenario: 移动构造

- **WHEN** 移动构造一个新的 `SharedPtr<T>` 从另一个 `SharedPtr<T>`
- **THEN** 新对象指向原指针，引用计数不变，源对象 `Get()` 返回 `nullptr`

#### Scenario: 移动赋值

- **WHEN** 将一个 `SharedPtr<T>` 移动赋值给另一个 `SharedPtr<T>`
- **THEN** 目标对象先 `SubRef` 旧指针，再接管新指针，源对象 `Get()` 返回 `nullptr`

### Requirement: SharedPtr 析构时 SubRef

`SharedPtr<T>` 析构时 SHALL 对其管理的指针调用 `SubRef()`。

#### Scenario: SharedPtr 析构调用 SubRef

- **WHEN** `SharedPtr<T>` 析构，且管理非空指针
- **THEN** 调用 `ptr->SubRef()`

### Requirement: SharedPtr 指针访问

`SharedPtr<T>` SHALL 提供 `operator->` 和 `operator*` 访问管理的指针。

#### Scenario: operator->

- **WHEN** 调用 `ptr->method()`，`ptr` 管理一个非空对象
- **THEN** 等价于调用 `rawPtr->method()`

#### Scenario: operator*

- **WHEN** 解引用 `*ptr`，`ptr` 管理一个非空对象
- **THEN** 返回对象引用

### Requirement: SharedPtr 的 Reset 操作

`SharedPtr<T>` SHALL 提供 `Reset(T* = nullptr)` 方法，替换管理的指针并正确维护引用计数。

#### Scenario: Reset 到新指针

- **WHEN** 调用 `ptr.Reset(newObj)`，`ptr` 当前管理 `oldObj`
- **THEN** 对 `oldObj` 调用 `SubRef()`，接管 `newObj` 并调用 `AddRef()`

#### Scenario: Reset 到空

- **WHEN** 调用 `ptr.Reset()` 或 `ptr.Reset(nullptr)`
- **THEN** 对旧对象调用 `SubRef()`，`Get()` 返回 `nullptr`

### Requirement: MakeShared 工厂函数

`utils::MakeShared<T>(Args&&...)` SHALL 构造一个 `T` 对象并返回管理它的 `SharedPtr<T>`。

#### Scenario: MakeShared 构造对象

- **WHEN** 调用 `MakeShared<MyObject>(args...)`
- **THEN** 返回一个管理新建 `MyObject` 的 `SharedPtr<MyObject>`，引用计数为 1

### Requirement: SharedPtr 编译期类型约束

`SharedPtr<T>` SHALL 通过 `static_assert` 要求 `T` 必须继承自 `utils::Ref`。

#### Scenario: 非 Ref 派生类编译失败

- **WHEN** 尝试实例化 `SharedPtr<SomeNonRefType>`
- **THEN** 编译失败并给出明确的错误信息
