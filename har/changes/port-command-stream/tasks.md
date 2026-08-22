# Tasks: Port CommandStream from Filament

依赖顺序：Handle → DriverAPI.inc/Driver → Dispatcher → CommandStream → ConcreteDispatcher → VulkanDriver 桩 → 编译 gate → 单测。

## 1. 移植 Handle.h

- [x] 1.1 新增 `include/Backend/Handle.h`：`Hw*` 前置声明、`HandleBase`（`nullid`/`operator bool`/`clear`/`GetId`/受保护拷贝移动）、`Handle<T>` 模板（比较运算、类型安全转换、显式拷贝移动赋值）、16 个资源别名；`assert_invariant` → `LOG_ASSERT`，不移植 io::ostream 调试友元
- [x] 1.2 单独编译验证 Handle.h（临时 TU 或并入下一步统一编译）

## 2. 升级 Driver 接口

- [x] 2.1 修改 `include/Backend/DriverAPI.inc`：TAGGED 两个宏中 `utils::ImmutableCString` → `utils::ImmutableString`；文件末尾追加种子方法清单（tick/beginFrame/flush/finish/resetState/createFence/destroyFence/terminate，8 条，类型名不加 `backend::` 前缀）
- [x] 2.2 重写 `include/Backend/Driver.h`：保持 `NS_UTILS::Ref` 基类与 `DECLARE_SHARE_PTR_CLASS(Driver)`；虚析构 `= default`；纯虚 `GetDispatcher() const noexcept`；虚 `Execute(std::function<void()> const&)` 默认 `fn()`；`DriverAPI.inc` 三组宏展开方法声明；`<functional>` include

## 3. 移植 Dispatcher

- [x] 3.1 新增 `src/command/Dispatcher.h`：前向声明 `Driver`/`CommandBase`，`using Execute`，`DriverAPI.inc` 展开 `Execute methodName##_` 成员
- [x] 3.2 新增 `src/command/CommandStreamDispatcher.h`：`ConcreteDispatcher<ConcreteDriver>` 静态 trampoline（`static_cast<ConcreteDriver&>` + `COMMAND_TYPE(...)::Execute`）与 `Make()` 填充表；不移植 SYSTRACE

## 4. 移植 CommandStream

- [x] 4.1 新增 `src/command/CommandStream.h`：`apply` 辅助、`CommandType`/`Command<&Driver::method>`（tuple 参数、静态 `Execute`、placement new）、`COMMAND_TYPE` 宏、`CustomCommand`、`CommandStream`（`Ref` + `DECLARE_SHARE_PTR_CLASS`、`DriverPtr` 构造、三组记录宏展开且裁剪 DEBUG 宏、`Execute`/`QueueCommand`/`Allocate`/`AllocatePod`/`DebugThreading`/`GetCircularBuffer`）；复用 `Command.h`，不重复定义 `CommandBase`/`NoopCommand`
- [x] 4.2 新增 `src/command/CommandStream.cpp`：`CommandStream::Execute`（`m_driver->Execute` 钩子内遍历命令链）、`QueueCommand`、`CustomCommand::Execute`；不移植 log/Profiler/tracing

## 5. VulkanDriver 适配

- [x] 5.1 修改 `src/vulkan/VulkanDriver.h`：声明 `Dispatcher GetDispatcher() const noexcept override`、`void terminate() override`
- [x] 5.2 修改 `src/vulkan/VulkanDriver.cpp`：实现 `GetDispatcher()`（`ConcreteDispatcher<VulkanDriver>::Make()`）与 `terminate()` 空桩，include `command/CommandStreamDispatcher.h`
- [x] 5.3 全量构建 `Backend` 静态库通过（编译 gate）

## 6. 单元测试

- [x] 6.1 新增 `tests/CommandStreamTest.cpp`：测试双 `NoopDriver : Driver`（方法调用计数器）；场景覆盖——命令链按序执行、RETURN 路径（`CreateFence` 同步取句柄 + 执行期调 `createFenceR`）、同步直调（`Terminate` 立即生效）、`Allocate` 的 NoopCommand 跳过辅助内存、`QueueCommand` lambda 执行、多帧 Flush/WaitForCommands/Execute/ReleaseBuffer 闭环
- [x] 6.2 构建并运行 `BackendTests`，全部测试通过
