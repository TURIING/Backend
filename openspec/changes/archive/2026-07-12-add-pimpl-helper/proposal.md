## Why

Backend 项目中需要频繁使用 PIMPL 惯用法来隐藏实现细节、减少头文件耦合和缩短编译时间。目前缺少一个标准的 PIMPL 辅助模板，每次都需要手写 `mImpl` 指针和构造/析构逻辑，样板代码多且容易出错。

## What Changes

- 在 Utils 库中新增 `Impl.h` 头文件，提供 `utils::Impl<T>` 模板类，封装 PIMPL 惯用法的构造、析构、拷贝和移动语义
- Utils 库引入 `utils` 命名空间
- 拷贝操作由 `T` 自身的拷贝能力自然决定，无需宏控制

## Capabilities

### New Capabilities
- `pimpl-helper`: 提供 `utils::Impl<T>` 模板类，封装 PIMPL 惯用法中的裸指针管理和生命周期

### Modified Capabilities
<!-- 无已有 capability 需要修改 -->

## Impact

- 新增文件：`3rd/Utils/include/Utils/Impl.h`
- Utils 库现有代码（`Utils.cpp`）需补充 `utils` 命名空间
- Backend 项目后续可通过 `#include <Utils/Impl.h>` 使用
