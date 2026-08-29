# Tasks: 移植 Filament HandleAllocator

## 1. 移植并适配头文件 `src/HandleAllocator.h`

- [x] 1.1 以 `Backend` 命名空间（`BEGIN_NS_BACKEND`）重写头文件骨架：`#pragma once`、include 顺序（`Backend/Handle.h` → 第三方 `Utils/*` → 标准库 → C 库）
- [x] 1.2 移植 `DebugTag` 基类：`utils::LockingPolicy::Mutex` + `std::unordered_map<HandleBase::HandleId, utils::ImmutableString>` 替换 `tsl::robin_map`，保留 `UTILS_GUARDED_BY` 注解
- [x] 1.3 移植 `HandleAllocator<P0, P1, P2>` 模板：`GetBucketSize` 编译期选桶、`AllocateAndConstruct` / `Allocate` / `DestroyAndConstruct` / `Construct` / `Deallocate` / `HandleCast` / `IsValid` / `GetHandleTag` / `AssociateTagToHandle`
- [x] 1.4 移植内嵌 `Allocator` 策略类：继承 `utils::AllocatorPolicyBase`，实现 2 参 `Alloc`/`Free` override 与 3 参 `Alloc(size, alignment, extra)` 非虚重载（按 size 选桶，`Pool<P0/P1/P2> = PoolAllocator<Pn, MIN_ALIGNMENT, sizeof(Node)>`），`Allocator(const HeapArea&, bool)` 构造
- [x] 1.5 移植 id 编解码：`HANDLE_AGE_*` / `HANDLE_HEAP_FLAG` / `HANDLE_INDEX_MASK` 常量、`IsPoolHandle` / `HandleToPointer` / `ArenaPointerToHandle`（改用 `GetArea().GetBegin()` / `Allocator::GetAlignment()`），提取 `HANDLE_TAG_KEY_MASK` 常量消除重复位运算
- [x] 1.6 适配分配/释放路径：`AllocateHandleInPool` 在 `mHandleArena.Alloc(...)` 后读 `pNode[-1].age`；`DeallocateHandleFromPool` 内做 age 校验（`LOG_CRITICAL` 致命）与递增后调 `mHandleArena.Free(p, SIZE)`
- [x] 1.7 用 `using HandleAllocatorVK = HandleAllocator<64, 160, 312>;` 替换 Filament 的 4 个宏定义
- [x] 1.8 按项目规范重写注释（删除 Filament license/文件头注释，仅保留"为什么"注释），核对命名与空行组织

## 2. 移植并适配实现文件 `src/HandleAllocator.cpp`

- [x] 2.1 移植 `DebugTag` 成员实现：构造 `reserve(512)`、`FindHandleTag`（无标签返回 `"(no tag)"`）、`WritePoolHandleTag` / `WriteHeapHandleTag`（锁内 `unordered_map` 赋值，`pos->second` 替换 `pos.value()`）
- [x] 2.2 移植 `Allocator` 构造函数：`maxHeapSize = min(area.GetSize(), HANDLE_INDEX_MASK * GetAlignment())`、`memset(area.GetData(), 0, maxHeapSize)`、三桶等分（`Pool<Pn>(p0, count * Pn)`），`LOG_WARN` 替换 `LOG(WARNING) <<`
- [x] 2.3 移植 `HandleAllocator` 构造/析构：`mHandleArena(size, flag)`（无 name 参数），析构泄漏检查（`LOG_ERROR` 替换 `PANIC_LOG`，`::free` 剩余映射内存）
- [x] 2.4 移植慢路径：`AllocateHandleSlow`（`::malloc` + `mId.fetch_add` + 上限 `LOG_CRITICAL` + `utils::UniqueLock` 写映射）、`DeallocateHandleSlow`、`HandleToPointerSlow`
- [x] 2.5 移植 `HandleCast`（池/堆两分支 use-after-free 检测，`LOG_CRITICAL` 替换 `FILAMENT_CHECK_POSTCONDITION`）、`GetHandleTag` / `AssociateTagToHandle`（`HANDLE_TAG_KEY_MASK` 截断）
- [x] 2.6 显式实例化 `template class HandleAllocator<64, 160, 312>;`（仅 Vulkan）
- [x] 2.7 按项目规范重写注释与命名

## 3. 构建验证

- [x] 3.1 重新 configure（GLOB 无 CONFIGURE_DEPENDS，需手动触发）并构建 `Backend` 库，确认零编译错误
- [x] 3.2 核对编译产物仅含 `HandleAllocator<64, 160, 312>` 实例化
