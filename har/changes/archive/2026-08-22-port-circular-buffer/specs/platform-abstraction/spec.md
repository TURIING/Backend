# Capability: platform-abstraction

## ADDED Requirements

### Requirement: 页大小查询

提供 `GetPageSize()` 返回系统内存页大小，按平台分支实现：unix/macOS 用 `sysconf(_SC_PAGESIZE)`，Windows 用 `GetSystemInfo`，其他平台兜底返回 4096。声明于 `architecture.h`，命名空间 `utils::arch`。

#### Scenario: unix/macOS 查询

- **WHEN** 在 unix 或 macOS 平台调用 `GetPageSize()`
- **THEN** 返回 `sysconf(_SC_PAGESIZE)` 的结果

#### Scenario: Windows 查询

- **WHEN** 在 Windows 平台调用 `GetPageSize()`
- **THEN** 返回 `SYSTEM_INFO.dwPageSize`

#### Scenario: 未知平台

- **WHEN** 在既非 unix/macOS 也非 Windows 的平台调用
- **THEN** 返回 4096

### Requirement: 页对齐工具

提供 `ALIGN_UP_PAGE(v, page)` 宏：将值向上对齐到页大小（低位掩码清零），供缓冲大小页对齐复用；另提供 `kCachelineSize` 常量（64）声明于 `architecture.h`。

#### Scenario: 页对齐

- **WHEN** 传入非页对齐值与页大小调用 `ALIGN_UP_PAGE`
- **THEN** 返回向上对齐到页边界的结果

### Requirement: ashmem 共享内存 fd 创建

提供 `ashmem_create_region(name, size)`（声明于 `ashmem.h`，命名空间 `utils`），创建可 mmap 的共享内存区域并返回 fd，失败返回 -1。按平台分支实现，与 Filament 行为一致：

- Android API ≥ 26（编译期 `__ANDROID_API__`）：`ASharedMemory_create`
- Android 老 API：`dlsym` 运行时探测 `ASharedMemory_create`（探测失败走 `/dev/ashmem` + `ASHMEM_SET_NAME`/`ASHMEM_SET_SIZE` ioctl）
- unix/macOS：临时文件 `mkstemp` + `unlink` + `ftruncate`
- Windows：`_mktemp` + `_open` + `_chsize`（与 Filament 一致，不改写）

#### Scenario: Android 创建

- **WHEN** 在 Android 平台调用 `ashmem_create_region`
- **THEN** API ≥ 26 使用 `ASharedMemory_create`；老 API 先 dlsym 探测，失败回退 `/dev/ashmem` ioctl

#### Scenario: unix/macOS 创建

- **WHEN** 在 unix 或 macOS 平台调用 `ashmem_create_region`
- **THEN** 在临时目录创建匿名临时文件并 `ftruncate` 到请求大小，返回其 fd（文件已 `unlink`，仅作为 mmap 共享 fd 使用）

#### Scenario: 创建失败

- **WHEN** 底层系统调用失败（如 `mkstemp` 返回 -1）
- **THEN** 返回 -1，调用方回退软模式

### Requirement: 临时目录获取

`ashmem.cpp` 内部提供平台化的临时目录获取，不依赖独立 Path 库：unix/macOS 返回 `/tmp`，Windows 用 `GetTempPath`。

#### Scenario: unix/macOS 临时目录

- **WHEN** ashmem 在 unix/macOS 创建临时文件
- **THEN** 使用 `/tmp` 作为临时目录（与 Filament linux 分支行为一致）

#### Scenario: Windows 临时目录

- **WHEN** ashmem 在 Windows 创建临时文件
- **THEN** 使用 `GetTempPath` 返回值作为临时目录

## MODIFIED Requirements

无

## REMOVED Requirements

无
