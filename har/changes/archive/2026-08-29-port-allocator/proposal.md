# Port Allocator from Filament

## Why

Backend 各模块（命令流、驱动层）需要可预测的高性能内存分配设施：arena 线性分配、对象池、作用域自动回收。Filament 的 `Allocator.h` 家族是生产验证的成熟实现，直接移植可复用其无分支分配、FreeList 对象池、ArenaScope 生命周期管理等设计，避免自研踩坑。

## What Changes

- 从 Filament 移植 `Allocator.h` / `Allocator.cpp` 全家族到 `3rd/Utils` 工具库，命名空间 `utils::`
- 按需求改造（相对 filament 原 API 均为 BREAKING，但属全新移植，无历史兼容负担）：
  - **Arena 去除 TrackingPolicy**：删除 `TrackingPolicy` 命名空间（Untracked/HighWatermark/Debug），Arena 不再持有 listener，删除 `getListener`/`setListener`/`emplaceListener`；同时删除 `name` 构造参数与 `getName()`
  - **Policy 基类 + 模板验证**：引入 `AllocatorPolicyBase` / `AreaPolicyBase` 空基类，所有分配器与 Area 继承之；Arena 模板用 `static_assert(std::is_base_of_v<...>)` 验证传入参数；**取消 `AreaPolicy` 命名空间**，`StaticArea`/`HeapArea`/`NullArea` 上提为平级类
  - **pointermath → `utils::pointer`**：新建 `include/Utils/Pointer.h`，原 `pointermath::add/align` 移植为 `pointer::Add/Align`
- 函数命名按库内规范转 PascalCase（`Alloc`/`Free`/`Reset`/`Rewind`/`Make`/`Destroy` 等；`STLAllocator::allocate/deallocate` 因 std 接口要求保持原名）
- 头文件按职责拆分，存放于 `include/Utils/Arena/` 目录（8 个头文件 + 2 个 cpp）
- 补齐移植依赖：新建 `mem/MemAlign.h`（对齐分配跨平台封装）、`LockingPolicy::Mutex = std::mutex`、`UTILS_RESTRICT`/`UTILS_MUL_OVERFLOW` 补入 `Compiler.h`

## Capabilities

### New Capabilities

- `allocator`: 内存分配能力（LinearAllocator/HeapAllocator/FreeList/PoolAllocator/Arena/ArenaScope/STLAllocator、Area 与 LockingPolicy 体系、pointer 指针工具）

### Modified Capabilities

无

## Impact

- 新增文件（`3rd/Utils` 下，CMake `GLOB_RECURSE` 自动收集，无需改构建配置）：
  - `include/Utils/Pointer.h`
  - `include/Utils/mem/MemAlign.h`
  - `include/Utils/Arena/AllocatorPolicy.h` / `Allocator.h` / `FreeList.h` / `PoolAllocator.h` / `Area.h` / `LockingPolicy.h` / `Arena.h` / `ArenaScope.h` / `STLAllocator.h`
  - `src/Arena/Allocator.cpp` / `src/Arena/FreeList.cpp`
  - `tests/AllocatorTest.cpp`
- 修改文件：
  - `include/Utils/Compiler.h`：新增 `UTILS_RESTRICT`、`UTILS_MUL_OVERFLOW`
  - `include/Utils/Utils.h`：引入新头文件
- 依赖：C++20 标准库（`<atomic>`/`<mutex>`/`<type_traits>`）、`std::mutex`；不依赖 spdlog/Log（原 HighWatermark/Debug 的日志依赖随 tracking 一并删除）
- 不改动既有代码行为与构建配置
