# Tasks: Port CircularBuffer from Filament

## 1. 平台抽象层（architecture）

- [x] 1.1 新建 `3rd/Utils/include/Utils/architecture.h`：`#pragma once`，命名空间 `utils::arch`，声明 `size_t GetPageSize() noexcept`，定义 `constexpr size_t kCachelineSize = 64`，定义 `ALIGN_UP_PAGE(v, page)` 宏（低位掩码清零对齐）
- [x] 1.2 新建 `3rd/Utils/src/architecture.cpp`：实现 `GetPageSize()`，平台分支 unix/macOS → `sysconf(_SC_PAGESIZE)`、WIN32 → `GetSystemInfo`、兜底 4096
- [x] 1.3 验证：配置后编译 Utils 目标无错误

## 2. ashmem 共享内存 fd（完整迁移）

- [x] 2.1 新建 `3rd/Utils/include/Utils/ashmem.h`：声明 `int ashmem_create_region(const char* name, size_t size)`（命名空间 `utils`，与 Filament 一致）
- [x] 2.2 新建 `3rd/Utils/src/ashmem.cpp`：迁移 Android 分支——`__ANDROID_API__ >= 26` 用 `ASharedMemory_create`；老 API 分支 `dlsym` 探测（去掉 api_level 依赖），失败回退 `/dev/ashmem` + `ASHMEM_SET_NAME`/`ASHMEM_SET_SIZE` ioctl
- [x] 2.3 迁移 unix/macOS 分支：`mkstemp` + `unlink` + `ftruncate`；临时目录用内部静态函数（unix/macOS → `/tmp`，Windows → `GetTempPath`），不依赖 Path 库
- [x] 2.4 迁移 Windows 分支：`_mktemp` + `_open` + `_chsize`（原样照搬，含 `_O_BINARY` 缺 flag 的现状）
- [x] 2.5 验证：macOS 上编译通过，调用 `ashmem_create_region` 返回有效 fd 且可 mmap

## 3. CircularBuffer 复刻

- [x] 3.1 新建 `3rd/Utils/include/Utils/buffer/CircularBuffer.h`：类声明（命名空间 `utils`），成员按项目规范 PascalCase（`Allocate`/`GetBuffer`/`GetUsed`/`Empty`/`Size`/`GetBlockSize`/`Alloc`/`Dealloc`），`= delete` 拷贝/移动，私有成员 `m_` 前缀，头文件注释声明单写者线程契约
- [x] 3.2 新建 `3rd/Utils/src/buffer/CircularBuffer.cpp`：静态成员 `sPageSize` 初始化（`GetPageSize()`）
- [x] 3.3 实现 `Alloc(size)` 硬模式：`ashmem_create_region` + 双 mmap（`MAP_PRIVATE`）+ guard 页；失败路径逐一 `munmap` + `close` 后回退软模式（匿名 `2*size+BLOCK_SIZE` + `mprotect` guard），`LOG_WARN` 输出软模式日志
- [x] 3.4 实现 Windows 分支（`VirtualAlloc` + `VirtualProtect` guard）与无 mmap 分支（`malloc(2*size)`），`Dealloc` 对应释放
- [x] 3.5 实现 `GetBuffer()`：返回 `{tail, head}` 区间；`pHead >= pEnd` 时硬模式折回 `pData + overflow`（`assert_invariant` 检查 `overflow <= mSize`）、软模式折回 `mData`；随后 `mTail = mHead`
- [x] 3.6 注释按项目规范重写：去 license 头、只写"为什么"不写"做什么"，非平凡逻辑（双映射、折回、guard）保留意图注释

## 4. 构建验证

- [x] 4.1 配置 CMake（GLOB 收集新源文件）并编译 Utils 目标
- [x] 4.2 编写临时验证程序（不入库）：构造 CircularBuffer，环形写入跨物理末尾数据，`GetBuffer()` 后跨边界读取校验内容一致（对照探索阶段实测）
- [x] 4.3 验证软模式路径：临时屏蔽硬模式成功条件，确认回退日志与功能正常
- [x] 4.4 清理临时验证程序，确认 `git status` 仅包含规划制品与新增源文件
