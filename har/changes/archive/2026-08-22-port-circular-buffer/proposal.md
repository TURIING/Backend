# Port CircularBuffer from Filament

## Why

Backend 的命令流（命令缓冲）需要一个高性能环形缓冲：单生产者连续写入、无锁指针推进、mmap 双映射实现零拷贝回绕。Filament 的 `CircularBuffer` 是经过生产验证的成熟实现，直接移植可复用其三层回退架构（mmap 硬模式 → mmap 软模式 → 平台兜底），避免自研踩坑。

## What Changes

- 从 Filament 移植 `CircularBuffer`（`.h`/`.cpp`）到 `3rd/Utils` 工具库，命名空间 `utils::`
- 新建 `architecture.h` 封装平台差异（页大小、缓存行大小、页对齐）
- 从 Filament 完整移植 `ashmem.h`/`ashmem.cpp`（Android/unix/Windows 三平台分支），`api_level` 逻辑内联不单独建文件
- 代码按项目规范重写：去 license 头、注释重写、函数名 PascalCase、常量 `kPascalCase`

## Capabilities

### New Capabilities

- `circular-buffer`: 环形缓冲能力（allocate/getBuffer 帧提交语义、硬/软双模式、guard page）
- `platform-abstraction`: 平台抽象能力（页大小查询、页对齐、ashmem 共享内存 fd 创建）

### Modified Capabilities

无

## Impact

- 新增文件（`3rd/Utils` 下，CMake `GLOB_RECURSE` 自动收集，无需改构建配置）：
  - `include/Utils/architecture.h` / `src/architecture.cpp`
  - `include/Utils/ashmem.h` / `src/ashmem.cpp`
  - `include/Utils/buffer/CircularBuffer.h` / `src/buffer/CircularBuffer.cpp`
- 依赖：复用现有 `Utils/Compiler.h`（`UTILS_LIKELY`/`UTILS_UNLIKELY`/`UTILS_NOINLINE`）、`Utils/Log.h`（`LOG_WARN`/`LOG_ASSERT`）、spdlog
- 不改动既有代码与构建配置
