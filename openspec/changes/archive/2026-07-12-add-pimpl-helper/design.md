## Context

Backend 项目已有 Utils 静态库（`3rd/Utils`），当前只有一个空的 `Utils.cpp` 占位文件。项目规范要求使用 C++20 和 `snake_case` 命名空间。需要为项目提供标准的 PIMPL 惯用法辅助模板，减少样板代码。

## Goals / Non-Goals

**Goals:**
- 提供 `utils::Impl<T>` 模板类，封装 PIMPL 惯用法中的 `new`/`delete` 和所有权语义
- 支持默认构造、参数转发构造、拷贝和移动语义
- 单头文件，声明与实现合一，便于使用
- 拷贝能力由 `T` 自然决定：`T` 不可拷贝则 `Impl<T>` 自动不可拷贝

**Non-Goals:**
- 不提供宏控制拷贝行为
- 不涉及 Utils 库的其他功能扩展
- 不修改 Backend 项目本身

## Decisions

### 1. 类名: `Impl`

`PrivateImplementation` 过长。`Impl` 简洁且是 C++ 圈对 "implementation" 的常用缩写。完整限定名为 `utils::Impl<T>`。

### 2. 命名空间: `utils`

遵循项目规范 `snake_case` 命名空间。选择扁平命名空间 `utils` 而非嵌套 `turiing::utils`，保持简洁。

### 3. 单文件结构: `Impl.h`

声明与实现合并于单个头文件，简化 include 路径。模板类的实例化需要实现在编译时可见，单文件避免了 `.inl` 分离的额外复杂度。

### 4. 拷贝策略: 跟随 T

```
T 可拷贝 → Impl<T> 可拷贝（new T(*rhs.m_pImpl)）
T 不可拷贝 → Impl<T> 自动不可拷贝（编译期报错）
```

无需 `UTILS_IMPL_NON_COPYABLE` 宏。C++ 模板在实例化时才检查成员函数体，T 不可拷贝时拷贝构造/赋值的 `new T(*rhs.m_pImpl)` 自然无法编译。

### 5. 成员变量命名

`m_pImpl`（`m_` 前缀 + `p` 指针前缀 + PascalCase，遵循项目私有成员变量规范中 `m_pPointer` 模式）

### 6. API 设计

```cpp
namespace utils {

template<typename T>
class Impl {
public:
    template<typename ... Args>
    explicit Impl(Args&& ...) noexcept;   // 参数转发构造
    Impl() noexcept;                       // 默认构造: new T
    ~Impl() noexcept;                      // delete m_pImpl
    Impl(Impl const& rhs) noexcept;        // 深拷贝
    Impl& operator=(Impl const& rhs) noexcept;

    // 移动: 内联，不需隐藏实现
    Impl(Impl&& rhs) noexcept : m_pImpl(rhs.m_pImpl) { rhs.m_pImpl = nullptr; }
    Impl& operator=(Impl&& rhs) noexcept {
        auto temp = m_pImpl; m_pImpl = rhs.m_pImpl; rhs.m_pImpl = temp; return *this;
    }

protected:
    T* m_pImpl = nullptr;
    inline T* operator->() noexcept { return m_pImpl; }
    inline T const* operator->() const noexcept { return m_pImpl; }
};

// 实现...
} // namespace utils
```

`operator->` 为 `protected`，子类通过 `this->operator->()` 或 `(*this)->method()` 访问底层对象。

## Risks / Trade-offs

- **[noexcept 与 new]** → 构造函数标记为 `noexcept` 但 `new T` 可能抛出 `std::bad_alloc`。当前遵循参考代码风格，后续如需要可移除 `noexcept`。
- **[编译错误信息]** → T 不可拷贝时错误指向 `new T(*rhs.m_pImpl)`，不如 `static_assert` 清晰。但复杂度换来的收益不大，暂不处理。

## Open Questions

无。
