# Design: Port CircularBuffer from Filament

## Context

Filament（Google 渲染引擎）backend 的命令缓冲使用 `CircularBuffer`：单生产者线程无锁写入命令，后台线程通过 `GetBuffer()` 的帧提交语义消费。核心技巧是用 mmap 把同一物理内存映射到两个相邻虚拟区间，写指针越过物理末尾后自动落入"shadow"区间，实现零拷贝回绕。项目 `3rd/Utils` 工具库已具备 `UTILS_LIKELY`/`UTILS_UNLIKELY`/`UTILS_NOINLINE`（Compiler.h）与 `LOG_WARN`/`LOG_ASSERT`（Log.h，spdlog 驱动），CMake 用 `GLOB_RECURSE src/*.cpp` 自动收集源文件。

## Goals / Non-Goals

**Goals:**
- 完整移植 `CircularBuffer` 三层回退架构（mmap 硬模式 → mmap 软模式 → Windows/malloc 兜底）
- 完整移植 `ashmem_create_region` 三平台分支（Android 新旧 API / unix / Windows）
- 新建 `architecture.h` 封装页大小等平台差异
- 代码符合项目规范：`utils::` 命名空间、PascalCase 函数、`kPascalCase` 常量、去 license 头、注释重写
- 不改动既有代码与构建配置（GLOB 自动收集）

**Non-Goals:**
- 不移植 Filament 的 `CommandBufferQueue`/`CommandStream`（消费端框架，后续独立工作）
- 不移植 `Path` 库（临时目录在 ashmem.cpp 内平台化适配）
- 不新建 `api_level` 模块
- 不引入单元测试目标（后续独立变更）

## Decisions

### D1: MAP_PRIVATE 双映射，忠实原版

实测验证（macOS）：`MAP_PRIVATE` 双映射不共享物理页（写时复制），但**正确性不依赖共享**——`head` 越过物理末尾的唯一途径是 `Allocate()` 后写入命令，被消费的 shadow 页必然已被写过，COW 私有副本即新命令数据。共享与否只影响物理内存（1× vs 2×），不影响功能。选择与 Filament 完全一致（`MAP_PRIVATE`），不引入 `MAP_SHARED` 的行为差异。

### D2: 硬模式 head 折回 `pData + overflow`

`GetBuffer()` 中硬模式将 `mHead` 折回 `pData + overflow`（而非 `mData`）：共享实现下物理 `[0, overflow)` 同时是虚拟 shadow 尾部（消费者正要读），折回 overflow 避免生产者覆盖未消费数据。软模式折回 `mData`（滑动窗口，数据永不搬移）。两分支均保持返回区间虚拟连续。

### D3: ashmem 完整迁移，api_level 内联

`api_level()` 在 Filament 中仅用于"避免在 API 19 上 dlsym"（一次无害调用）。去掉该依赖后行为等价：老 Android 分支直接 `dlsym` 探测，失败（符号不存在）回退 `/dev/ashmem` ioctl。不新建 `api_level.h`/`.cpp`。

### D4: 临时目录不搬 Path 库

`Path::getTemporaryDirectory()` 在 Filament 中：linux 硬编码 `/tmp`，Windows 用 `GetTempPath`。在 ashmem.cpp 内部以静态函数实现同等行为，避免引入整个 Path 工具库。

### D5: Windows `_open` 缺 flag 照搬

Filament Windows 分支 `_open(tmpPath, _O_BINARY)` 缺 `_O_CREAT|_O_RDWR`，理论上打不开文件。用户决策照搬（与上游同病），保持逐字一致。

### D6: 依赖映射与命名规范

| Filament 依赖 | 本项目替代 |
|---|---|
| `utils/architecture.h` | 新建 `architecture.h`（`utils::arch::GetPageSize`） |
| `utils/Compiler.h` | 已有（同名宏） |
| `utils/Logger.h`/`ostream.h` | `LOG_WARN`（已有） |
| `utils/Panic.h` | `LOG_ASSERT` + `LOG_CRITICAL`（已有） |
| `utils/ashmem.h` | 新建（完整迁移） |
| `utils/debug.h` | `LOG_ASSERT`（NDEBUG 下为空，语义一致） |

类成员函数按项目规范改 PascalCase：`allocate`→`Allocate`、`getBuffer`→`GetBuffer`、`getUsed`→`GetUsed`、`size`→`Size`、`getBlockSize`→`GetBlockSize`、`alloc`→`Alloc`、`dealloc`→`Dealloc`；`ashmem_create_region` 保持 snake_case 不变（平台约定 API，与 Filament 一致）。

## Risks / Trade-offs

- [MAP_PRIVATE 下物理内存 2×] → 与 Filament 行为一致，接受；后续如需 1× 可单独变更为 MAP_SHARED（接口不变）
- [Windows ashmem 分支可能失效（_open 缺 flag）] → 与上游一致，Windows 平台将回退软模式，功能不受影响
- [Android 老 API 分支无法在 macOS 上编译验证] → 编译期 `__ANDROID_API__` 分支隔离，语法风险低；按 Filament 原文逐字迁移
- [页大小运行时查询（sysconf）非 constexpr] → `GetBlockSize()` 为静态函数，页对齐用带 page 参数的宏，不依赖编译期常量

## Open Questions

无（探索阶段已全部确认）
