# Tasks: Port Allocator from Filament

## 1. 依赖补齐（宏与对齐分配）

- [x] 1.1 `include/Utils/Compiler.h` 新增 `UTILS_RESTRICT`（GCC/Clang 为 `__restrict`，否则空），与 `UTILS_LIKELY` 同组放置
- [x] 1.2 `include/Utils/Compiler.h` 新增 `UTILS_MUL_OVERFLOW(a, b, out)`（GCC/Clang 走 `__builtin_mul_overflow`，其他平台 fallback 除法回验）
- [x] 1.3 新建 `include/Utils/mem/MemAlign.h`：`utils::AlignedAlloc(size, alignment)` / `AlignedFree(p)` 跨平台封装（POSIX `posix_memalign`、Windows `_aligned_malloc/_aligned_free`），alignment 2 的幂与 `sizeof(void*)` 倍数断言

## 2. Pointer 工具

- [x] 2.1 新建 `include/Utils/Pointer.h`：`namespace utils::pointer`，`Add(P*, size_t)` / `Align(P*, size_t)` / `Align(P*, size_t, size_t)`（2 的幂断言保留），按项目规范重写注释

## 3. Area 与 LockingPolicy

- [x] 3.1 新建 `include/Utils/Arena/Area.h`：`AreaPolicyBase` 空基类 + `StaticArea`（外部区间，可拷贝/移动/Swap，`GetData/GetBegin/GetEnd/GetSize`）+ `HeapArea`（malloc 拥有，`pointer::Add` 算 end，禁拷贝）+ `NullArea`（占位），均继承基类；取消 `AreaPolicy` 命名空间
- [x] 3.2 新建 `include/Utils/Arena/LockingPolicy.h`：`namespace LockingPolicy`，`NoLock`（lock/unlock 空操作）、`using Mutex = std::mutex`

## 4. Allocator 家族

- [x] 4.1 新建 `include/Utils/Arena/AllocatorPolicy.h`：`AllocatorPolicyBase` 空基类
- [x] 4.2 新建 `include/Utils/Arena/Allocator.h`：`LinearAllocator`（branch-less `Alloc`、`Rewind/Reset/GetCurrent/Allocated/Available/Swap`、uint32_t 偏移注释）+ `HeapAllocator`（`AlignedAlloc/AlignedFree`）+ `LinearAllocatorWithFallback`（私有组合两者，`IsHeapAllocation`），均继承 `AllocatorPolicyBase`，命名转 PascalCase
- [x] 4.3 新建 `include/Utils/Arena/FreeList.h`：`FreeList`（`Pop/Push/GetFirst`、Node 预埋、debug 区间断言）+ `AtomicFreeList`（`HeadPtr` 32+32 位原子、tagged CAS、acquire/release 语义）
- [x] 4.4 新建 `include/Utils/Arena/PoolAllocator.h`：`PoolAllocator<ELEMENT_SIZE, ALIGNMENT, OFFSET, FREELIST>`（`static_assert` Node 容纳、参数断言）+ `PoolAllocatorWithFallback` + `ObjectPoolAllocator`/`ThreadSafeObjectPoolAllocator` 别名（`UTILS_MAX` 改 `std::max`），均继承 `AllocatorPolicyBase`
- [x] 4.5 新建 `src/Arena/Allocator.cpp`：`LinearAllocator` 构造/移动/`Swap`、`LinearAllocatorWithFallback::Alloc/Reset` 实现体（含堆块 `AlignedFree` 释放）
- [x] 4.6 新建 `src/Arena/FreeList.cpp`：`FreeList::Init/FreeList`、`AtomicFreeList` 构造实现体

## 5. Arena 层

- [x] 5.1 新建 `include/Utils/Arena/Arena.h`：`Arena<AllocatorPolicy, LockingPolicy, AreaPolicy = HeapArea>`——两个 `static_assert(is_base_of_v<...Base, ...>)`；构造 `Arena(size, args...)` / `Arena(AreaPolicy&&, args...)`（无 name）；`Alloc` 三档重载 + `Alloc<T>(count,...)`（平凡析构 SFINAE + 溢出防护）；`Free` 三档；`Reset/Rewind/GetCurrent`；`Make<T>/Destroy<T>`；`GetAllocator/GetArea`；`std::lock_guard<LockingPolicy>` 保护；禁拷贝可 `Swap`；删除全部 listener 相关与 `getName`
- [x] 5.2 新建 `include/Utils/Arena/ArenaScope.h`：`ArenaScope<ARENA>`（finalizer 链逆序析构 + 析构时 `Rewind`，`pointer::Add` 定位 Finalizer 后对象）
- [x] 5.3 新建 `include/Utils/Arena/STLAllocator.h`：`STLAllocator<TYPE, ARENA>`（完整 std 分配器接口，`allocate/deallocate` 保持原名，`rebind`/`propagate_on_container_move_assignment`/`is_always_equal`，按 Arena 地址判等）

## 6. 集成

- [x] 6.1 `include/Utils/Utils.h` 引入新头：`Pointer.h`、`mem/MemAlign.h`、`Arena/` 下全部头文件（按现有分组风格）
- [x] 6.2 构建验证：`cmake --build build/3rd/Utils`（或等价命令）编译通过，无警告错误

## 7. 测试

- [x] 7.1 新建 `tests/AllocatorTest.cpp`：Pointer 对齐/偏移用例；LinearAllocator 顺序分配/空间不足/Rewind/Reset；HeapAllocator 对齐分配释放；WithFallback 回退堆与 `IsHeapAllocation`/`Reset` 释放；FreeList LIFO；AtomicFreeList 多线程压力（并发 Pop/Push 每块恰好一次）；PoolAllocator 复用与参数断言；Arena 全接口（含 `Make/Destroy`、溢出防护、平凡析构 SFINAE 编译期用例）；ArenaScope 作用域回退与逆序析构；STLAllocator 配 `std::vector`；Area 三实现行为
- [x] 7.2 运行 `UtilsTests`：全部用例通过（gtest）；可选 TSAN 构建验证 AtomicFreeList 无数据竞争
- [x] 7.3 收尾核对：`static_assert` 非法类型用例确认编译期报错（可用负向编译测试或注释说明）；spec 中所有 Scenario 有对应用例

## 8. 归档准备（apply 完成后由 har-archive 处理）

- [x] 8.1 确认 `har/specs/` 同步：将 allocator 能力域 spec 复制/移动到 `har/specs/allocator/spec.md` 并标记本变更完成

## 9. 基类接口演进（会话追加）

- [x] 9.1 `AllocatorPolicyBase`/`AreaPolicyBase` 增加纯虚交集接口（两参 `Alloc`/`Free`；`GetData`/`GetSize`）与虚析构；`AllocatorPolicy.h` 并入 `Allocator.h`
- [x] 9.2 派生类补齐 override：Linear/Heap/WithFallback/Pool/PoolWithFallback 的 `Alloc`/`Free`（Linear/Pool 拆出 3 参重载），Static/Heap/Null 的 `GetData`/`GetSize`
- [x] 9.3 concept 约束：`DerivedFromAllocatorPolicy`/`DerivedFromAreaPolicy` 替换 `static_assert`（`requires` 子句）；`Resettable`/`Rewindable`/`Currentable` 按使用点约束 `Arena::Reset`/`Rewind`/`GetCurrent`
- [x] 9.4 清理悬空 include（`Utils.h`/`PoolAllocator.h` 中已删除的 `AllocatorPolicy.h`）；测试补 concept 断言与负向编译验证（非法类型 constraints 报错、`HeapArena::Reset` 禁用）
