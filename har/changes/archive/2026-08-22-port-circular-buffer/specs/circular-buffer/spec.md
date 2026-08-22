# Capability: circular-buffer

## ADDED Requirements

### Requirement: 环形缓冲分配

提供 `CircularBuffer` 类，构造时按请求大小分配内存；`Allocate(size_t)` 在缓冲内分配连续内存并返回指针，分配大小不得超过 `Size()` 字节（超限为前置条件错误，debug 构建断言）。

#### Scenario: 顺序分配

- **WHEN** 调用方连续调用 `Allocate()` 分配若干块
- **THEN** 返回的指针单调递增，且各块在缓冲内不重叠

#### Scenario: 分配超限

- **WHEN** `GetUsed() + s > Size()` 时调用 `Allocate(s)`
- **THEN** debug 构建触发断言（`LOG_ASSERT`）；release 构建行为未定义（由 guard page 提供崩溃保护而非静默损坏）

### Requirement: 帧提交语义

`GetBuffer()` 返回自上次调用以来的已分配区间 `{tail, head}` 并重置写指针；`Empty()` 表示自上次提交以来无新分配；`GetUsed()` 返回当前未提交字节数。调用方必须确保返回区间不再被使用时才允许继续 `Allocate()` 覆盖。

#### Scenario: 提交并重置

- **WHEN** 写入若干命令后调用 `GetBuffer()`
- **THEN** 返回 `[上一次 head, 当前 head)` 区间，且随后 `Empty()` 为 true

#### Scenario: 写指针越过物理末尾（硬模式）

- **WHEN** 已分配区间超过缓冲物理末尾且硬模式生效（ashmem fd 有效）
- **THEN** 写指针折回物理开头 + overflow 偏移，返回区间保持虚拟连续，消费方跨边界读取内容正确

#### Scenario: 写指针越过物理末尾（软模式）

- **WHEN** 已分配区间超过缓冲物理末尾且软模式生效（ashmem fd 无效）
- **THEN** 写指针折回缓冲起点（滑动窗口），返回区间保持虚拟连续，消费方跨边界读取内容正确

### Requirement: 硬模式双映射

在支持 mmap 的平台，使用 ashmem fd 将同一物理内存映射到两个相邻虚拟地址区间（`MAP_PRIVATE`），并在 shadow 区之后映射 guard 页（`PROT_NONE`，大小 = 页大小）。分配失败时（含 guard 页映射失败）清理所有已建立的映射并回退软模式。

#### Scenario: 双映射成功

- **WHEN** ashmem fd 创建成功且两次 mmap 与 guard 映射均成功
- **THEN** 采用硬模式，`GetBuffer()` 走折回逻辑，类析构时释放全部映射并关闭 fd

#### Scenario: 双映射失败回退

- **WHEN** 任一映射步骤失败（mmap 返回 `MAP_FAILED` 或地址不符）
- **THEN** 逐一 `munmap` 已建映射、关闭 fd，回退软模式，并输出 warning 日志

### Requirement: 软模式回退

硬模式失败时，分配 `2 * size + 页大小` 的匿名 mmap 空间，末尾页 `mprotect` 为 `PROT_NONE` 作 guard；分配失败时通过后置检查终止（`LOG_CRITICAL`）。

#### Scenario: 软模式启用

- **WHEN** ashmem 创建失败或双映射失败
- **THEN** 使用匿名双倍空间 + guard 页，缓冲功能正常可用，仅物理内存消耗加倍

### Requirement: 平台兜底分配

不支持 mmap 的平台使用 `malloc(2 * size)`，Windows 使用 `VirtualAlloc`（`MEM_RESERVE | MEM_COMMIT`）+ `VirtualProtect` guard 页；`Dealloc()` 与分配方式对应释放（`free`/`VirtualFree`/`munmap`）。

#### Scenario: Windows 分配

- **WHEN** 在 Windows 平台构造缓冲
- **THEN** 使用 `VirtualAlloc` 分配双倍空间，末尾 guard 页设为 `PAGE_NOACCESS`，析构时 `VirtualFree`

#### Scenario: 无 mmap 平台分配

- **WHEN** 在无 mmap 平台构造缓冲
- **THEN** 使用 `malloc(2 * size)`，析构时 `free`

### Requirement: 生命周期与线程契约

类不可拷贝、不可移动；析构释放全部资源。缓冲仅允许单线程写入（head/tail 无锁推进），该契约须在头文件注释中声明。

#### Scenario: 拷贝/移动被禁用

- **WHEN** 尝试拷贝或移动构造/赋值 `CircularBuffer`
- **THEN** 编译期报错（`= delete`）

#### Scenario: 单写者

- **WHEN** 多个线程同时调用 `Allocate()`
- **THEN** 行为未定义；正确用法为单生产者线程独占写入，消费方仅通过 `GetBuffer()` 返回的区间读取

### Requirement: 查询接口

提供 `Size()`（缓冲总大小，常量）、`GetBlockSize()`（系统页大小，静态）、`Empty()`、`GetUsed()` 查询接口。

#### Scenario: 大小查询

- **WHEN** 调用 `Size()` 与 `GetBlockSize()`
- **THEN** 分别返回构造时大小与系统页大小（`GetPageSize()` 结果）

## MODIFIED Requirements

无

## REMOVED Requirements

无
