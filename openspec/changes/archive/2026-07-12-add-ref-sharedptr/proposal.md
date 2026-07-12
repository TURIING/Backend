## Why

项目缺乏统一的共享所有权机制。当前代码库中只有原始指针和独占所有权的 PIMPL 辅助类。随着 Backend 项目扩展——例如 `Platform` 对象可能被多个模块共享、渲染资源需要在不同组件间传递——需要一套侵入式引用计数基础设施来安全地管理对象生命周期。

## What Changes

- 在 Utils 子模块中新增 `utils::Ref` 基类，提供原子引用计数和自销毁能力
- 在 Utils 子模块中新增 `utils::SharedPtr<T>` 智能指针模板，自动管理 `Ref` 派生对象的 `AddRef`/`SubRef`
- 在 Utils 子模块中新增 `MakeShared<T>()` 工厂函数
- 新增 gtest 测试框架及 `Ref`/`SharedPtr` 的单元测试

## Capabilities

### New Capabilities

- `ref-counted-base`: `utils::Ref` 基类，提供 `AddRef`/`SubRef`/`GetRefCount` 方法，引用计数归零时自动 `delete this`
- `shared-ptr`: `utils::SharedPtr<T>` 智能指针模板，构造/拷贝时 `AddRef`，析构/重置时 `SubRef`，支持移动语义和空指针

### Modified Capabilities

<!-- 无需修改现有 spec -->

## Impact

- **新增文件**: `3rd/Utils/include/Utils/mem/Ref.h`、`3rd/Utils/include/Utils/mem/SharedPtr.h`
- **新增测试**: `3rd/Utils/tests/` 目录及 gtest 测试文件
- **依赖新增**: gtest（通过 git submodule 引入到 `3rd/Utils/3rd/gtest/`）
- **CMake**: 需修改 `3rd/Utils/CMakeLists.txt`，添加 gtest 子目录及测试 target
- **不破坏现有代码**: 纯新增功能
