# Proposal: Port CommandStream from Filament

## Why

Filament backend 移植已就位 `CircularBuffer`、`CommandBufferQueue`、`Command.h`（`CommandBase`/`NoopCommand`），但命令流枢纽 `CommandStream` 尚未移植，`Driver` 仍是空壳。`CommandStream` 是连接 Driver 接口与命令缓冲的桥梁：记录线程把 Driver 调用序列化为类型擦除命令写入环形缓冲（无锁），执行线程逐条回放。移植后"记录 → Flush → 执行 → 归还"闭环打通，为后续 VulkanDriver 真实实现提供基础设施。

## What Changes

- 移植 `CommandStream.h`/`.cpp`：`CommandType` 模板命令、`CustomCommand`、`CommandStream` 主体与执行循环
- 移植 `Dispatcher.h`（函数指针表）与 `CommandStreamDispatcher.h`（`ConcreteDispatcher` 派发生成模板）
- 移植 `Handle.h`：`HandleBase` + `Handle<T>` 类型安全资源句柄（`createFence` 等返回值路径的前提）
- `Driver` 从空壳升级为抽象接口：`Ref` 基类 + 虚函数（`GetDispatcher`/`Execute`/同步方法）+ `DriverAPI.inc` 种子方法清单（8 条真实方法名，覆盖全部三条宏路径）
- `VulkanDriver` 补虚函数桩，保持可编译
- 新增 gtest 单元测试

非 BREAKING：`Driver` 类形状不变（仍继承 `Ref`、保留 `DriverPtr`），VulkanDriver 仅新增 override。

## Capabilities

### New Capabilities

- `handle`: 类型安全资源句柄（`Handle<T>` 模板 + 各资源别名）
- `driver-interface`: Driver 抽象接口与 `DriverAPI.inc` 种子方法清单
- `dispatcher`: 函数指针派发表与 `ConcreteDispatcher` 模板
- `command-stream`: 命令流主体（`CommandType`/`CustomCommand`/`CommandStream` + 执行循环）

### Modified Capabilities

- 无（`circular-buffer`/`command-buffer-queue` 语义不变，仅被新的调用方使用）

## Impact

- `include/Backend/Handle.h`（新增）
- `include/Backend/Driver.h`（重写：空壳 → 抽象接口）
- `include/Backend/DriverAPI.inc`（修正 TAGGED 宏类名 + 追加 8 条种子方法）
- `src/command/Dispatcher.h`、`src/command/CommandStream.h`、`src/command/CommandStream.cpp`、`src/command/CommandStreamDispatcher.h`（新增）
- `src/vulkan/VulkanDriver.h`、`src/vulkan/VulkanDriver.cpp`（补虚函数桩）
- `tests/CommandStreamTest.cpp`（新增）
- 构建：无 CMake 改动（`file(GLOB_RECURSE)` 自动收集源文件）
