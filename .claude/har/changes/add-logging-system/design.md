## Context
Utils 模块当前缺少日志系统。项目使用 C++20，遵循特定命名和代码规范（PascalCase 类名、`utils` 命名空间、`#pragma once` 等）。Utils 是独立的 git 仓库，通过 submodule 方式被 Backend 引用。参考了 `learn-ray-tracing` 项目中基于 spdlog 的日志实现。

## Goals / Non-Goals

**Goals:**
- 为 Utils 模块提供基于 spdlog 的异步日志系统
- 支持 Android (logcat)、Windows (WinColor 控制台)、macOS (ANSI 终端) 三种平台 sink 自动选择
- 提供简洁的宏接口（6 级日志）
- 保持与参考实现一致的 API 设计
- spdlog 以 git submodule 方式获取，对上层透明

**Non-Goals:**
- 不实现文件日志 sink（当前只需控制台/logcat 输出）
- 不实现运行时 sink 切换（编译期平台分发已足够）
- 不实现日志格式配置接口（使用硬编码的合理默认值）
- Linux 平台（暂无需求，未来可复用 macOS 路径扩展）

## Decisions

### 决策 1: spdlog 放在 Utils 仓库内作为 submodule（方案 A）
**选择**: spdlog 作为 Utils 的 submodule (`3rd/spdlog/`)，通过 `target_link_libraries(Utils PUBLIC spdlog::spdlog)` 导出依赖。

**理由**:
- 符合项目规范"第三方库一律使用 git submodule"
- PUBLIC 链接使得上层只需链接 Utils 即可获得 spdlog 头文件路径
- 对 Backend 完全透明，不需要修改 Backend 的 CMakeLists.txt
- 如果将来多个模块需要 spdlog，可以提升到 Backend 层级，但当前 Utils 是唯一的消费者

**替代方案**: 放在 Backend 的 `3rd/` 目录下
- 缺点: Utils 作为独立仓库，其 CMake 无法引用上层目录的 spdlog
- 缺点: 其他人单独使用 Utils 仓库时需要额外配置

### 决策 2: 平台 sink 在 .cpp 中通过预处理器分发
**选择**: 在 `Log.cpp` 的 `Init()` 中使用 `#if defined(__ANDROID__)` / `#elif defined(_WIN32)` / `#elif defined(__APPLE__)` 编译期分发。

**理由**:
- 实现细节完全封装在 .cpp 中，头文件保持干净
- 不需要条件编译宏污染头文件
- spdlog 各平台 sink 头文件只需在 .cpp 中包含

### 决策 3: Android 使用独立日志格式
**选择**: Android 使用 `[%P:%t] [%l] %v`，桌面平台使用 `[%Y-%m-%d %H:%M:%S.%e] [%P:%t] [%l] %v`。

**理由**:
- logcat 自带时间戳和 tag 信息，重复添加冗余
- 桌面终端没有 logcat 那样的元数据展示，需要自包含

### 决策 4: LOG_CRITICAL 保持 flush + abort 行为
**选择**: 与参考实现一致，`LOG_CRITICAL` 先刷新缓冲区，再输出日志，最后调用 `std::abort()`。

**理由**:
- CRITICAL 级别的含义是不可恢复的致命错误，终止程序是合理行为
- 先 flush 确保日志不丢失（便于事后排查）

### 决策 5: 模板方法 + 宏的两层接口
**选择**: 类提供模板成员方法，宏封装单例调用。

模式:
```cpp
template<typename... Args>
void Info(fmt::format_string<Args...> fmt, Args&&... args);

#define LOG_INFO(...) ::utils::Log::Instance().Info(__VA_ARGS__)
```

**理由**:
- 模板方法提供编译期格式校验（`fmt::format_string`）
- 宏简化调用，无需每次写 `Log::Instance()`
- 与参考实现完全一致

## Risks / Trade-offs

- **spdlog 版本锁定**: submodule 锁定特定 commit，如需更新需手动操作 → 记录在 README 中
- **Android liblog 依赖**: `android_sink` 依赖 NDK 的 `liblog`，CMake 需 `find_library(log-lib log)` → spdlog 的 CMake 已处理此依赖
- **线程池开销**: 异步日志需要一个后台线程 → 开销极小（单线程阻塞等待），对性能敏感的路径可用 `LOG_TRACE` 并在 Release 中设置较高级别过滤

## Open Questions
无 — 所有设计点已在探索阶段达成一致。
