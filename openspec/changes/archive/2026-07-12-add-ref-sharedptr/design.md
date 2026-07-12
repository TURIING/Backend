## Context

Backend 是一个 C++20 静态库项目，目前使用原始指针管理对象生命周期。随着 `Platform` 等对象需要在多个模块间共享，需要一套侵入式引用计数基础设施。

Utils 子模块 (`3rd/Utils/`) 用于存放通用工具类，已有 `utils::Impl<T>` (PIMPL 辅助)。新增的引用计数工具放在 `3rd/Utils/include/Utils/mem/` 目录下，延续 Utils 的模块化约定。

## Goals / Non-Goals

**Goals:**
- 提供 `utils::Ref` 基类，继承即可获得原子引用计数 + 自销毁能力
- 提供 `utils::SharedPtr<T>` 智能指针，自动管理 Ref 派生对象的 AddRef/SubRef
- 提供 `MakeShared<T>()` 工厂函数便于构造
- header-only 实现，零外部依赖（除 `std::atomic`）
- gtest 单元测试覆盖

**Non-Goals:**
<!-- 多线程安全已在 D5 中实现 -->
- 不实现 `WeakPtr`（循环引用暂不处理，文档警告即可）
- 不实现 `SharedFromThis()`（后续按需添加）
- 不替代 `std::shared_ptr`——本项目有自己的命名和代码风格约定

## Decisions

### D1: 侵入式引用计数 (Intrusive)

计数器内置于对象中（`Ref` 基类），而非 `SharedPtr` 分配独立的控制块。

**理由**: 避免额外的堆分配；一个对象只需继承 `Ref` 即可被 `SharedPtr` 管理，无需在 `SharedPtr` 构造时分配控制块。与 COM `IUnknown`、`boost::intrusive_ptr` 模式一致。

### D2: 初始引用计数为 0

**理由**: 裸 `new` 构造的对象 refCount=0，只有在被 `SharedPtr` 接管后才有引用。如果裸指针从未被 `SharedPtr` 接管，`SubRef` 不会被调用，不会误删。语义清晰，无隐式假设。

### D3: virtual 析构函数

`Ref` 的析构函数声明为 `virtual`。`SubRef()` 中通过 `delete this` 销毁对象时，必须正确调用派生类析构。

### D4: Header-only 实现

`Ref` 和 `SharedPtr<T>` 所有方法均在头文件中定义。`Ref` 的方法足够简短（原子操作），`SharedPtr<T>` 是模板类，两者都适合 header-only。

### D5: SubRef 使用 memory_order_acq_rel

`SubRef()` 的 `fetch_sub` 使用 `std::memory_order_acq_rel`。release 语义保证当前线程在释放引用之前的所有写入对其他线程可见；acquire 语义保证 `delete this` 时已获取所有其他线程的写入，确保析构安全。

`AddRef()` 保持 `memory_order_relaxed`——递增计数不依赖对象状态，无需额外的同步开销。

### D6: SharedPtr 禁止跨类型转换

`SharedPtr<T>` 仅接受恰好为 `T*` 类型的裸指针。不支持 `SharedPtr<Derived>` 到 `SharedPtr<Base>` 的隐式转换。

**理由**: 保持第一版简单。跨类型转换涉及 `static_cast` 和继承关系的编译期验证，增加模板复杂度。实际需要时再添加。

### D7: Ref 禁止拷贝和移动

引用计数对象的语义决定了它不应被值拷贝——拷贝构造/赋值会破坏引用计数的唯一性。移动同理。

### D8: gtest 通过 git submodule 引入

遵循项目规范，第三方库使用 git submodule 管理。gtest 作为 Utils 的开发依赖，存放到 `3rd/Utils/3rd/gtest/`，保证 Utils 子模块自包含——独立 clone 也能编译运行测试。测试代码放在 Utils 子模块的 `tests/` 目录下。

## Risks / Trade-offs

- **循环引用**: 无 `WeakPtr` 支持，文档中明确警告，由调用方避免 → 后续可按需实现
- **裸指针误用**: 用户在 `SharedPtr` 之外手动调用 `AddRef`/`SubRef` 会破坏计数 → 文档警告，参考 C++ 标准库对 `shared_ptr` 的类似立场
<!-- 已升级为 acq_rel，此风险不再适用 -->
- **header-only 编译时间**: 模板实例化增加编译开销，但由于类规模小且方法简短，影响可忽略
- **嵌套 submodule**: gtest 作为 Utils 的嵌套 submodule，clone Backend 时需 `git submodule update --init --recursive` 才能拉取完整测试依赖
