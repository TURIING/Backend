## ADDED Requirements

### Requirement: spdlog 作为 Git Submodule 获取
spdlog 库 SHALL 通过 `git submodule` 方式获取，存放到 Utils 仓库的 `3rd/spdlog/` 目录。

#### Scenario: 添加 spdlog submodule
- **WHEN** 在 Utils 仓库中执行 `git submodule add https://github.com/gabime/spdlog.git 3rd/spdlog`
- **THEN** Utils 的 `.gitmodules` 中应包含 `[submodule "3rd/spdlog"]` 条目
- **AND** `3rd/spdlog/` 目录包含 spdlog 源码（至少 `include/spdlog/spdlog.h`）

### Requirement: spdlog 作为 CMake 目标编译
spdlog SHALL 通过 CMake `add_subdirectory` 编译，为 Utils 库提供 `spdlog::spdlog` 链接目标。

#### Scenario: spdlog 集成到 Utils CMake
- **WHEN** Utils 的 CMakeLists.txt 执行 `add_subdirectory(3rd/spdlog)`
- **THEN** 生成 `spdlog::spdlog` target 供 Utils 链接
- **AND** 上层链接 Utils 时自动传递 spdlog 头文件路径

### Requirement: Log 单例类
Log 类 SHALL 以单例模式提供全局日志能力，通过 `Log::Instance()` 访问。

#### Scenario: 获取 Log 实例
- **WHEN** 调用 `Log::Instance()`
- **THEN** 返回全局唯一的 Log 实例引用
- **AND** 多次调用返回同一实例

### Requirement: 六级日志宏
日志系统 SHALL 提供 6 级日志宏：TRACE、DEBUG、INFO、WARN、ERROR、CRITICAL。

#### Scenario: 各级别日志输出
- **WHEN** 调用 `LOG_INFO("message {}", value)`
- **THEN** 以 INFO 级别输出格式化日志
- **AND** 低于当前日志级别的调用被忽略

### Requirement: CRITICAL 日志强制终止
LOG_CRITICAL 宏 SHALL 在输出日志后刷新缓冲区并调用 `std::abort()` 终止程序。

#### Scenario: 触发 CRITICAL 日志
- **WHEN** 调用 `LOG_CRITICAL("fatal error: {}", reason)`
- **THEN** 日志缓冲区被刷新，日志输出到目标
- **AND** 程序通过 `std::abort()` 终止

### Requirement: 异步日志
日志系统 SHALL 使用 spdlog 异步 logger，通过后台线程处理日志写入。

#### Scenario: 异步写入不阻塞主线程
- **WHEN** 主线程频繁调用日志宏
- **THEN** 日志消息进入队列（8192 条），后台线程异步消费
- **AND** 队列满时 block 等待（不丢日志）

### Requirement: Init/Shutdown 生命周期管理
日志系统 SHALL 提供 `Init()` 和 `Shutdown()` 管理生命周期。`Init()` 多次调用安全。

#### Scenario: 初始化日志系统
- **WHEN** 程序启动时调用 `Log::Instance().Init()`
- **THEN** 创建异步 logger、线程池、平台 sink
- **AND** 后续日志调用正常工作

#### Scenario: 重复初始化安全
- **WHEN** 多次调用 `Init()`
- **THEN** 第二次及后续调用直接返回，不重复创建资源

#### Scenario: 关闭日志系统
- **WHEN** 程序退出前调用 `Log::Instance().Shutdown()`
- **THEN** 刷新并释放所有日志资源
- **AND** 之后可再次 `Init()` 重新初始化

### Requirement: SetLevel 动态调整日志级别
日志系统 SHALL 提供 `SetLevel(LogLevel)` 在运行时动态调整最低输出级别。

#### Scenario: 调整日志级别
- **WHEN** 调用 `SetLevel(LogLevel::Warn)`
- **THEN** 低于 Warn 级别的 TRACE、DEBUG、INFO 日志不再输出

### Requirement: Flush 强制刷新
日志系统 SHALL 提供 `Flush()` 强制将缓冲区中的日志立即输出。

#### Scenario: 强制刷新缓冲区
- **WHEN** 调用 `Log::Instance().Flush()`
- **THEN** 队列中所有待输出日志被立即写入目标

### Requirement: 平台 Sink 自动选择（Android）
在 Android 平台上，日志 SHALL 通过 `android_sink_mt` 输出到 Android logcat。

#### Scenario: Android 平台日志输出
- **WHEN** 在 Android 平台调用 `Init()`
- **THEN** 创建 `spdlog::sinks::android_sink_mt` sink，tag 为 `"Utils"`
- **AND** 日志格式使用简洁模式 `[%P:%t] [%l] %v`（logcat 自带时间戳）
- **AND** 不设置颜色格式

### Requirement: 平台 Sink 自动选择（Windows）
在 Windows 平台上，日志 SHALL 通过 `wincolor_stdout_sink_mt` 输出到控制台。

#### Scenario: Windows 平台日志输出
- **WHEN** 在 Windows 平台调用 `Init()`
- **THEN** 创建 `spdlog::sinks::wincolor_stdout_sink_mt` sink
- **AND** 日志格式使用 `[%Y-%m-%d %H:%M:%S.%e] [%P:%t] [%l] %v`
- **AND** 6 级日志分别设置不同颜色（灰/青/绿/黄/红/白底红字）

### Requirement: 平台 Sink 自动选择（macOS）
在 macOS 平台上，日志 SHALL 通过 `stdout_color_sink_mt` 输出到终端。

#### Scenario: macOS 平台日志输出
- **WHEN** 在 macOS 平台调用 `Init()`
- **THEN** 创建 `spdlog::sinks::stdout_color_sink_mt` sink
- **AND** 日志格式使用 `[%Y-%m-%d %H:%M:%S.%e] [%P:%t] [%l] %v`
- **AND** 6 级日志分别设置不同颜色（灰/青/绿/黄/红/白底红字）

### Requirement: LogLevel 枚举
日志系统 SHALL 定义 `LogLevel` 枚举类，包含 6 个级别。

#### Scenario: 使用 LogLevel 设置级别
- **WHEN** 调用 `SetLevel(LogLevel::Debug)`
- **THEN** Debug 及以上级别日志输出，Trace 被忽略
- **AND** LogLevel 值映射到 spdlog 对应级别

### Requirement: Flush 级别
日志系统 SHALL 在 Warn 及以上级别日志触发时自动刷新缓冲区。

#### Scenario: Warn 级别触发自动刷新
- **WHEN** 输出 `LOG_WARN(...)` 或更高级别日志
- **THEN** 日志缓冲区被自动刷新

### Requirement: fmt 格式化支持
日志方法 SHALL 接受 `fmt::format_string<Args...>` 格式字符串，支持编译期格式校验。

#### Scenario: 编译期格式检查
- **WHEN** 编写 `LOG_INFO("value: {}", 42)`
- **THEN** 正常编译通过
- **AND** 如果格式字符串与参数不匹配，编译器产生错误

### Requirement: Utils.h 聚合
`Utils.h` 聚合头文件 SHALL 包含 `Log.h`，使得引用 Utils 的上层代码自动获得日志能力。

#### Scenario: 通过 Utils.h 使用日志
- **WHEN** 代码 `#include <Utils/Utils.h>`
- **THEN** LOG_* 宏可用
- **AND** 无需单独 `#include <Utils/Log.h>`

### Requirement: spdlog 对上层透明
Utils 库 SHALL NOT 在公开头文件中暴露 spdlog 的实现细节。上层代码只需链接 Utils 目标，无需直接依赖 spdlog。

#### Scenario: 上层不感知 spdlog
- **WHEN** 上层项目 `target_link_libraries(MyApp Utils)` 并使用 `LOG_INFO(...)` 宏
- **THEN** 无需显式 `find_package(spdlog)` 或链接 `spdlog::spdlog`
- **AND** 日志正常工作
