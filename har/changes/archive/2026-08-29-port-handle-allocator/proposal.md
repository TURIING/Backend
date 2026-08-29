# Proposal: 移植 Filament HandleAllocator

## Why
本项目已具备类型安全句柄（`Handle<T>`，handle 域）与底层分配器家族（allocator 域的 Arena/PoolAllocator），但缺少句柄的生产/消费层。Filament 的 `HandleAllocator` 以三层池化 + age 机制实现句柄高效分配、释放与 use-after-free 检测，是 Filament backend 句柄生命周期的核心组件。移植后为本项目 Driver API 的句柄路径提供完整的生命周期管理基础。

## What Changes
- 新增 `src/HandleAllocator.h`：模板类 `HandleAllocator<P0, P1, P2>`（含 `DebugTag` 调试标签基类），提供 `allocate` / `allocateAndConstruct` / `deallocate` / `handle_cast` / `is_valid` / `destroyAndConstruct` / `construct` 等句柄生命周期 API
- 新增 `src/HandleAllocator.cpp`：非模板成员实现（构造/析构、慢路径分配、tag 管理、内嵌 Allocator 策略构造）与 Vulkan 显式实例化
- 全部适配本项目 Utils API 与代码规范（见 design.md）；不照搬 Filament 注释（含 license 头），按项目注释规范重写

## Capabilities

### New Capabilities
- `handle-allocator`: 句柄分配与生命周期管理（三层池化分桶 + age 检测 + 慢路径溢出 + 调试标签）

### Modified Capabilities
<!-- 无：handle 域仅覆盖句柄类型，allocator 域仅覆盖底层分配器，均不需修改 -->

## Impact
- 代码：新增 `src/HandleAllocator.h` / `src/HandleAllocator.cpp`；根 CMakeLists 的 `file(GLOB_RECURSE src/*.cpp)` 自动纳入新 .cpp，无需修改 CMake（注意 GLOB 无 CONFIGURE_DEPENDS，需重新 configure 一次）
- 依赖：`3rd/Utils`（Arena / PoolAllocator / HeapArea / ImmutableString / Mutex / LockGuard / UniqueLock / Compiler 宏 / Log）；不再依赖 Filament 的 `tsl::robin_map`，以 `std::unordered_map` 替代
- 不修改任何现有文件；本变更仅落库组件，不接入 VulkanDriver
