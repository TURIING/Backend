# Port StructureOfArrays from Filament

## Why

引擎/驱动需要把一组相关联的异构数组（如 位置 + 法线 + 句柄）以 **AoS 直觉访问、SoA 内存布局** 存储：按字段读写 `elementAt<I>(i)`，按行整体搬运/排序，且整组共享一次分配、cache 友好。Filament 的 `StructureOfArrays.h` 是生产验证的 SoA 模板容器，直接移植避免自研踩坑（偏移对齐计算、代理迭代器、Field 引用等）。

## What Changes

- 从 Filament 移植 `StructureOfArrays.h`（784 行，仅头文件模板）到 `3rd/Utils`，命名空间 `utils`
- **类型改名**：`StructureOfArraysBase` → `SoaBase`，`StructureOfArrays` → 别名 `Soa`（用户要求，名称过长）
- 按 Utils 现状适配（全新移植，无历史兼容负担）：
  - **默认分配器换为 Utils `HeapAllocator`**：Filament 内部 `alloc(size, align)` / `free(ptr)` → `Alloc(size, align)` / `Free(p, size)`（Utils 分配器两参风格，释放携带实际尺寸）；模板参数仍保留注入能力
  - **`slice<E>()` 返回值由 `utils::Slice` 改 `std::span`**（Utils 无 Slice 类型，C++20 已启用）
  - **缺失宏不补 `Compiler.h`**：`UTILS_ALWAYS_INLINE`/`UTILS_UNROLL`/`UTILS_UNUSED` 分别以类内 `inline`（天然内联）、删除 unroll pragma、`[[maybe_unused]]` 替代
- 函数命名按库内规范转 PascalCase（`SetCapacity`/`EnsureCapacity`/`PushBack`/`PopBack`/`Resize`/`Clear`/`ElementAt`/`CopyRange` 等），成员转 `m_` 前缀
- 头文件注释按项目规范重写：去掉 Apache 版权头，只保留非平凡逻辑的中文注释

## Capabilities

### New Capabilities

- `soa`: 结构体数组（SoA）容器——`SoaBase<Allocator, Elements...>` + `Soa<Elements...>` 别名：单缓冲多列存储、列访问、行代理访问、随机访问迭代器（STL 算法）、Field 字段引用、跨分配器 `CopyRange`

### Modified Capabilities

无

## Impact

- 新增文件（均在 `3rd/Utils` 子模块；头文件与测试由 CMake `GLOB_RECURSE` 自动收集，src 无 cpp 产出）：
  - `include/Utils/Soa.h`
  - `tests/SoaTest.cpp`
- 修改文件：
  - `include/Utils/Utils.h`：引入 `Soa.h`（伞头按现有分组风格追加）
- 依赖：C++20 标准库（`<array>`/`<tuple>`/`<span>`/`<type_traits>`/`<iterator>`）、Utils 现有 `Arena/Allocator.h`（`HeapAllocator` 与分配策略概念）、`Compiler.h` 宏（`UTILS_NOINLINE`/`UTILS_RESTRICT`/`UTILS_UNLIKELY`，均已存在）
- 不改动既有代码行为与构建配置
