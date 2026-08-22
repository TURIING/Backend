# Design: Port CommandStream from Filament

## Context

`port-circular-buffer` 与 `port-command-buffer-queue` 已交付 `Utils::CircularBuffer`（PascalCase API）与 `CommandBufferQueue`（`src/command/`），`Command.h` 已含 `CommandBase`/`NoopCommand`（`ExecuteFn` 类型别名、`kObjectAlignment`、`LOG_ASSERT` 映射）。本次移植 `CommandStream.h` 及其配套：`Dispatcher`、`ConcreteDispatcher`、`Handle.h`，并将 `Driver` 从空壳升级为抽象接口。git 历史 `9a8c84e^` 有一版旧移植（单文件、继承 `Ref`、无派发机制）可作参考底稿。

探索阶段已确认的现状与事实：

- 本地 `Driver.h` 为 `class Driver : public NS_UTILS::Ref {}` + `DECLARE_SHARE_PTR_CLASS(Driver)`；`SharedPtr<T>` 有 `is_base_of_v<Ref, T>` 静态断言 → **Driver 继承 Ref 是结构性要求，不是可选风格**
- 本地 `DriverAPI.inc` 只有宏机制（APPLY/PAIR_ARGS/各 `DECL_DRIVER_API_*`），无方法清单；TAGGED 宏引用 `utils::ImmutableCString`，但本地 Utils 类名为 `ImmutableString` —— 潜伏 bug，启用种子集即触发
- 本地无 `ThreadUtils`/`assert_invariant`/`UTILS_RESTRICT`/tracing；`LOG_ASSERT`（NDEBUG 空）为既定断言替代
- 用户决策：① CommandStream 持 `DriverPtr`（Ref 语义）② 补 gtest 单测 ③ 真实方法名 + 顺带移植 `Handle.h`

## Goals / Non-Goals

**Goals:**
- 移植 `Handle.h` 到 `include/Backend/`（`HandleBase` + `Handle<T>` + 16 个资源别名）
- `Driver` 升级为抽象接口：虚析构、纯虚 `GetDispatcher()`、虚 `Execute(std::function<void()> const&)`（默认直调）、`DriverAPI.inc` 展开方法声明
- `DriverAPI.inc` 追加 8 条真实方法种子集（tick/beginFrame/flush/finish/resetState/createFence/destroyFence/terminate），覆盖三条宏路径；修正 TAGGED 宏类名
- 移植 `Dispatcher.h`（函数指针表）与 `CommandStreamDispatcher.h`（`ConcreteDispatcher<T>::Make()`）
- 移植 `CommandStream.h`/`.cpp`：`CommandType`/`CustomCommand`/`CommandStream`（Ref + `DriverPtr` + `m_dispatcher`），裁剪调试设施
- `VulkanDriver` 补 `GetDispatcher()`/`terminate()` 桩，保持全库可编译
- `tests/CommandStreamTest.cpp` gtest 单测：命令链执行、RETURN/同步路径、NoopCommand 跳过、queueCommand

**Non-Goals:**
- 不移植 `DriverEnums.h`/`PipelineState.h`/`Program.h` 等参数类型头文件（种子集参数均为内建类型/`FenceHandle`/`ImmutableString`）
- 不补全 `DriverAPI.inc` 全部 179 条方法（后续独立工作，往同一 inc 追加即可）
- 不实现 Vulkan 真实驱动逻辑
- 不移植 `utils::ThreadUtils`（`std::this_thread::get_id()` 替代）、`Profiler`/`Tracing`/`CallStack`、`DEBUG_COMMAND_STREAM` log（RTTI demangle）
- 不改动 `CircularBuffer`/`CommandBufferQueue`/`Command.h` 既有代码

## Decisions

### D1: 文件布局

`include/Backend/Handle.h`（公有类型）、`src/command/Dispatcher.h`、`src/command/CommandStream.h`、`src/command/CommandStream.cpp`、`src/command/CommandStreamDispatcher.h`。`CommandStream.h` include 顺序按规范：本项目 `.h`（`Backend/Driver.h`、`Dispatcher.h`、`Command.h`）→ `""` → 第三方（`Utils/buffer/CircularBuffer.h`）→ 标准库。根 CMake `file(GLOB_RECURSE src/*.cpp)` 自动收集新 `.cpp`，无需改构建；`tests/CMakeLists.txt` 同样自动收集新测试。

### D2: Driver 生命周期形态（用户决策 1）

`Driver : NS_UTILS::Ref` 保持不变，`CommandStream` 持有 `DriverPtr`（`m_driver`），构造时 `m_dispatcher(driver->GetDispatcher())` 复制一份 `Dispatcher`。同步宏展开用 `*m_driver`（解引用后 `apply`），执行循环用 `Driver& driver = *m_driver`。保活语义：`CommandStreamPtr` 持 `DriverPtr`，执行线程跑命令期间驱动不会被释放。与历史移植（`9a8c84e^`）一致。`CommandStream` 自身也 `DECLARE_SHARE_PTR_CLASS(CommandStream)`。

### D3: DriverAPI.inc 种子集与命名

8 条声明追加在 inc 末尾（宏机制之后），方法名保持 Filament 原版拼写（`tick`/`beginFrame`/...）——这是 `COMMAND_TYPE(method)` 机制的结构性要求（方法名必须与 `Driver` 声明严格一致），也是后续粘贴剩余 171 条的前提。类型名不加 `backend::` 前缀（inc 在 `namespace Backend` 内展开，`FenceHandle` 直接可用）。宏驱动的成员/方法名（`beginFrame_`、`beginFrame(...)`、trampoline `beginFrame`）同为拼写例外，在代码注释中说明。修正 `DECL_DRIVER_API_TAGGED_R_N`/`DECL_DRIVER_API_SYNCHRONOUS_TAGGED_N` 中 `utils::ImmutableCString` → `utils::ImmutableString`。

### D4: 调试设施裁剪

`DEBUG_COMMAND_BEGIN/END`（依赖 `Driver::debugCommandBegin/End` 虚函数）从 `DECL_DRIVER_API*` 展开中删除，`AutoExecute` 辅助随之删除；`DEBUG_COMMAND_STREAM` log（`CommandType::Command::log()`，RTTI demangle + CallStack）不移植，`CommandStream.cpp` 的显式实例化块删除；`FILAMENT_TRACING_*`/`Profiler`（`execute()` 内）删除，`m_usePerformanceCounter` 成员删除。`Driver::execute` 钩子保留（架构保真，默认 `fn()`），`CommandStream::Execute` 在其内遍历命令链。

### D5: 依赖映射

| Filament | 本项目 |
|---|---|
| `assert_invariant`（utils/debug.h） | `LOG_ASSERT` |
| `utils::ThreadUtils::getThreadId/isThisThread` | `std::this_thread::get_id()`（NDEBUG 下 `m_threadId` 成员不定义） |
| `utils::ImmutableCString` | `utils::ImmutableString`（同时修正 TAGGED 宏） |
| `UTILS_RESTRICT` | 不使用（无裸引用成员） |
| `FILAMENT_TRACING_*`/`Profiler`/`CallStack`/io::ostream | 删除 |
| `backend::` 限定名 | 命名空间内不加前缀 |

### D6: 复用 Command.h

`CommandBase`/`NoopCommand` 已在 `src/command/Command.h`（`kObjectAlignment`/`Align`/`ExecuteFn`/`Execute`），`CommandStream.h` include 复用，不重复定义。`CustomCommand` 与 `CommandType` 只被 `CommandStream` 使用，留在 `CommandStream.h`（对齐 Filament 布局）。

### D7: VulkanDriver 桩

`VulkanDriver` 声明 `Dispatcher GetDispatcher() const noexcept override`（实现：`return ConcreteDispatcher<VulkanDriver>::Make();`，顺带在真实类型上验证派发表生成）与 `void terminate() override`（空桩）。`GetDispatcher` 名不受宏影响（`Driver.h` 手写声明，PascalCase）。`execute()` 钩子有默认实现，无需覆盖。`Create()` 形态不变。

### D8: 测试策略（用户决策 2）

`tests/CommandStreamTest.cpp` 内置测试双 `NoopDriver : Driver`（计数器记录各方法调用次数/参数），测试场景：命令链执行顺序、RETURN 路径（`CreateFence` 同步取句柄 + 执行期调用 `createFenceR`）、同步直调（`Terminate` 立即生效、缓冲无写入）、`Allocate` 的 NoopCommand 跳过（辅助块后命令仍执行）、`QueueCommand` lambda 执行、空缓冲 `Flush` 短路（复用既有队列语义）。测试经 `CommandBufferQueue` + `CommandStream` 走完整闭环（记录 → Flush → WaitForCommands → Execute → ReleaseBuffer），单线程确定性，不引入线程测试。

### D9: 命名规范

公有方法 PascalCase（`Execute`/`QueueCommand`/`Allocate`/`AllocatePod`/`GetCircularBuffer`/`DebugThreading`/`Make`），私有成员 `m_` 前缀（`m_driver`/`m_currentBuffer`/`m_dispatcher`/`m_threadId`/`m_args`/`m_command`/`m_next`），常量 `kPascalCase`（`kObjectAlignment` 复用）。宏生成的名称（方法名、`methodName##_`、trampoline 名）为 D3 声明的拼写例外。注释按项目规范：中文、只写意图、不写文件头。

## Risks / Trade-offs

- [`DriverAPI.inc` 被四份文件 include（Driver.h/Dispatcher.h/CommandStream.h/CommandStreamDispatcher.h），宏定义顺序敏感] → 严格按 Filament 原版结构组织：各文件 include 前 `#define` 同名宏、include 后不 `#undef`（后续 include 重新定义），保持与 原版一致
- [种子集一旦启用，TAGGED 宏展开引用不存在的 `ImmutableCString` 导致编译失败] → D3 同步修正为 `ImmutableString`，任务 2.1 先行
- [`Driver` 变抽象后 `VulkanDriver` 编译失败] → 任务顺序保证桩实现与接口升级同一批次落地，编译 gate 在两者之后
- [debug 线程断言误报（单测与记录线程必须一致）] → 测试单线程执行，`DebugThreading()` 显式调用
- [`NoopCommand` 构造 `reinterpret_cast` 不可 constexpr] → `Command.h` 已按此处理（非常量 constexpr），沿用
- [空 tuple（0 参命令如 `tick`）`std::apply` 展开] → 标准行为，无特殊处理
- [`SharedPtr(new VulkanDriver())` 双计数风险] → 既有模式，`Ref` 初始计数为 0，`SharedPtr` 构造时 `AddRef` 置 1，生命周期由所有权方唯一管理，本次不改变

## Open Questions

- 无（三个决策点已由用户确认，边界完整）
