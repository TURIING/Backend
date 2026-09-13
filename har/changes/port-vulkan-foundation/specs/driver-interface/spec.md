## ADDED Requirements

### Requirement: Driver 生命周期与回调虚函数

`Driver` SHALL 新增以下 5 个虚函数，与上游 `backend/private/backend/Driver.h` 对应（命名按项目 `PascalCase` 规范，上游为 `camelCase`）：

| 本项目 | 上游 | 纯度 | 语义 |
|---|---|---|---|
| `Purge() noexcept` | `purge()` | 纯虚 | 由主线程（非渲染线程）周期性调用，驱动在此执行用户回调 |
| `ScheduleCallback(CallbackHandler*, void*, CallbackHandler::Callback)` | `scheduleCallback(...)` | 纯虚 | 由渲染线程调用；handler 非空时经其派发，为空时留给 `Purge()` |
| `SetUnrecoverableError() noexcept` | `setUnrecoverableError()` | 虚（默认空实现） | 标记不可恢复错误：中断全部待决围栏等待并阻止后续等待 |
| `DebugCommandBegin(CommandStream*, bool, char const*) noexcept` | `debugCommandBegin(...)` | 纯虚 | 命令开始钩子，仅在调试构建或手动开启时被调用 |
| `DebugCommandEnd(CommandStream*, bool, char const*) noexcept` | `debugCommandEnd(...)` | 纯虚 | 命令结束钩子 |

`Driver.h` SHALL include `Backend/CallbackHandler.h`，并以前置声明 `class CommandStream;` 引入命令流类型——`CommandStream` 位于 `src/command/`（私有层），公共头反向依赖它会破坏分层。

这 5 个虚函数 SHALL NOT 进入 `DriverAPI.inc`——后者承载的是经命令流排队的记录端方法，而这 5 个由 `DriverBase` 直接实现或由引擎直接调用。

#### Scenario: 子类必须实现四者

- **WHEN** 声明 `Driver` 的具体子类（不论是否经 `DriverBase`）
- **THEN** `Purge` / `ScheduleCallback` / `DebugCommandBegin` / `DebugCommandEnd` 必须被实现，否则编译失败；`SetUnrecoverableError` 可继承默认空实现

#### Scenario: 头文件不依赖 src/

- **WHEN** include `Backend/Driver.h`
- **THEN** 不引入 `src/` 下的任何头文件（`CommandStream` 为前置声明）

#### Scenario: 命名差异有据可查

- **WHEN** 移植上游调用点 `driver.scheduleCallback(handler, user, cb)`
- **THEN** 改写为 `driver.ScheduleCallback(handler, user, cb)`——依据 `backend-utils` 的映射约定，不重新讨论命名策略

### Requirement: VulkanDriver 提前转入 DriverBase

`VulkanDriver` SHALL 在本变更即改为继承 `DriverBase`（而非等待变更 7），并在构造初始化列表中把 `DriverConfig` 传给 `DriverBase`。

理由：`Driver` 新增的 4 个纯虚函数若无人实现，`VulkanDriver` 立即编译失败；而 `DriverBase` 恰好实现了全部 4 个。变更 7 本就要做这一步继承切换，提前做可避免留下一个无法编译的中间态。本变更**不**为 `VulkanDriver` 添加任何其他 `DriverBase` 能力的使用（`Purge` / `ScheduleCallback` 的实际使用在变更 6–7）。

#### Scenario: 构建保持绿色

- **WHEN** 追加 5 个虚函数后全量构建
- **THEN** `VulkanDriver` 经 `DriverBase` 满足接口，构建无错误

#### Scenario: 行为不变

- **WHEN** 运行 `bin/BackendTests`
- **THEN** 8 帧往返输出与本变更前一致，exit 0（`DriverBase` 构造启动的 ServiceThread 在析构时被 join）

## MODIFIED Requirements

### Requirement: 种子方法清单

`DriverAPI.inc` SHALL 保持既有 15 条方法的拼写与签名不变：

- 种子集：`tick` / `beginFrame` / `flush` / `finish` / `resetState` / `createFence` / `destroyFence` / `terminate`
- 缓冲族：`CreateVertexBufferInfo` / `CreateVertexBuffer` / `CreateBufferObject` / `CreateIndexBuffer` / `DestroyBufferObject` / `DestroyIndexBuffer` / `SetVertexBufferObject`

种子集 SHALL 保持上游小写拼写；缓冲族 SHALL 保持项目 `PascalCase` 拼写。

原要求中「方法名 SHALL 保持 Filament 原版拼写（与 `Driver` 声明严格一致，是 `COMMAND_TYPE` 机制的前提）」的表述 SHALL 修正——该表述不准确：`#define COMMAND_TYPE(method) CommandType<decltype(&Driver::method)>::Command<&Driver::method>`（`src/command/CommandStream.h:69`）从 `&Driver::method` 推导，**方法名无需与上游一致**。保持上游拼写仅为对照便利。

本变更**不**追加任何方法到 `DriverAPI.inc`（143 条清单属变更 7）。

#### Scenario: 方法总数不变

- **WHEN** 统计 `DriverAPI.inc` 的 `DECL_DRIVER_API*` 条目
- **THEN** 仍为 15 条

#### Scenario: 既有方法逐字未变

- **WHEN** 对比本变更前后的 `DriverAPI.inc`
- **THEN** 15 条逐字一致

#### Scenario: 新增虚函数不进入 inc

- **WHEN** 检索 `DriverAPI.inc`
- **THEN** 不出现 `Purge` / `ScheduleCallback` / `SetUnrecoverableError` / `DebugCommandBegin` / `DebugCommandEnd`
