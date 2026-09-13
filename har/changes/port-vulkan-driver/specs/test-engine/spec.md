## ADDED Requirements

### Requirement: 三角形端到端验证用例

`tests/` SHALL 提供端到端绘制验证，覆盖从交换链创建到像素读回的完整路径：

1. **交换链**：`CreateSwapChainHeadless(256, 256, 0)`
2. **程序**：内嵌最小顶点/片元 SPIR-V，经 `Backend::Program` 构造 → `CreateProgram`
3. **图元**：`CreateVertexBufferInfo`（1 buffer、1 attribute）→ `CreateVertexBuffer` → `CreateBufferObject` → 上传三顶点 → `CreateRenderPrimitive`
4. **渲染目标**：`CreateDefaultRenderTarget` + `MakeCurrent`
5. **绘制**：`BeginFrame` → `BeginRenderPass`（clear 黑色）→ `BindPipeline` → `BindRenderPrimitive` → `DrawArrays(0, 3, 1)` → `EndRenderPass` → `Commit`
6. **校验**：`ReadPixels` 读回中心像素，断言颜色为片元着色器输出值

`Engine` SHALL 为上述步骤提供转发方法（与既有 `CreateIndexBuffer` / `DestroyIndexBuffer` 同模式），使测试代码经 `CommandStream` 走完整异步路径。

#### Scenario: 完整路径可运行

- **WHEN** 运行三角形用例
- **THEN** 全程无 `LOG_CRITICAL`、无 Vulkan 校验层错误，进程 exit 0

#### Scenario: 中心像素颜色正确

- **WHEN** 读回绘制完成的交换链中心像素
- **THEN** 其 RGB 与片元着色器输出一致（允许 ±1 的精度误差）

#### Scenario: 无描述符集的管线可用

- **WHEN** 以不含 descriptor set 布局的 `Program` 创建管线并绘制
- **THEN** 绘制成功，验证「零描述符集」路径可行

### Requirement: SPIR-V 二进制的来源与存放

三角形用例的 SPIR-V SHALL 以预编译字节数组的形式存放于 `tests/`，**不在测试运行时编译着色器**。

理由：不引入运行期 `glslang` 依赖；产物可读、可校验、可版本控制。

生成方式 SHALL 在文件注释中记录（使用的编译器与命令行），使产物可复现。

#### Scenario: 无运行期编译依赖

- **WHEN** 构建并运行测试
- **THEN** 不链接 `glslang` / `spirv-tools`，不调用外部编译器进程

#### Scenario: 产物可复现

- **WHEN** 按文件注释中的命令行重新生成
- **THEN** 得到等价的 SPIR-V 二进制

### Requirement: 降级验证路径

若三角形用例因环境限制（SPIR-V 生成不可用、`vkCreateGraphicsPipelines` 在 MoltenVK 上失败、headless 交换链不支持渲染）无法通过，SHALL 提供降级验证：

**清屏用例**：`BeginRenderPass`（clear 为已知颜色）→ `EndRenderPass` → `Commit` → `ReadPixels` 校验 clear 颜色。

降级用例覆盖 `beginRenderPass` / `endRenderPass` / `commit` / `readPixels`，但**不覆盖** `bindPipeline` / `draw`。

采用降级方案时 SHALL：
1. 在 tasks 中显式记录降级原因
2. 在 `vulkan-driver` spec 的适配约束中标注 `bindPipeline` / `draw` 路径**未经端到端验证**
3. 保留三角形用例代码（以编译开关或跳过标记隔离），使环境具备时可重新启用

#### Scenario: 降级可验证清屏

- **WHEN** 采用降级方案运行
- **THEN** 读回的像素等于 clear 颜色，证明显式渲染通道与提交路径可用

#### Scenario: 降级原因被记录

- **WHEN** 采用降级方案
- **THEN** tasks 中记录具体原因（SPIR-V / 管线创建 / headless 渲染中的哪一环失败）

### Requirement: 销毁路径验证

测试 SHALL 覆盖 `VulkanDriver::DestroyResources()` 的两个关键约束：

1. **幂等**：连续调用 `terminate()` 两次不崩溃
2. **无泄漏**：`vmaDestroyAllocator` 不触发 `Some allocations were not freed` 断言

#### Scenario: 重复 terminate

- **WHEN** 在既有 `Terminate()` 之后再次调用 `Terminate()`
- **THEN** 不崩溃、无重复释放

#### Scenario: 退出无 VMA 泄漏

- **WHEN** 用例结束后进程退出
- **THEN** 无 `Some allocations were not freed` 断言

#### Scenario: 读回线程已 join

- **WHEN** 检查退出路径
- **THEN** 无「线程仍在运行但对象已析构」的崩溃或挂起

## MODIFIED Requirements

### Requirement: Engine 记录端 API

`Engine` SHALL 在既有转发方法（`BeginFrame` / `Flush` / `Finish` / `CreateFence` / `DestroyFence` / `CreateIndexBuffer` / `DestroyIndexBuffer` / `ResetState` / `QueueCommand` / `Terminate`）之外，新增绘制路径的转发方法：

- 交换链：`CreateSwapChainHeadless` / `DestroySwapChain` / `MakeCurrent` / `Commit`
- 程序：`CreateProgram` / `DestroyProgram`
- 图元：`CreateVertexBufferInfo` / `CreateVertexBuffer` / `CreateBufferObject` / `UpdateBufferObject` / `CreateRenderPrimitive` / `DestroyRenderPrimitive`
- 渲染目标：`CreateDefaultRenderTarget` / `DestroyRenderTarget`
- 绘制：`BeginRenderPass` / `EndRenderPass` / `BindPipeline` / `BindRenderPrimitive` / `DrawArrays` / `Scissor`
- 读回：`ReadPixels`

全部转发方法 SHALL 与既有方法同模式（转发到 `m_stream`），使测试经完整命令流路径而非直接调用驱动。

#### Scenario: 转发走命令流

- **WHEN** 调用 `Engine::BeginRenderPass(...)`
- **THEN** 经 `m_stream->BeginRenderPass(...)` 记录到命令流，由驱动线程执行

#### Scenario: 既有方法不受影响

- **WHEN** 运行既有 8 帧往返
- **THEN** 行为与本变更前一致

### Requirement: App 单例运行

`App::Run` SHALL 保留既有的 8 帧创建/销毁往返作为回归用例，并新增三角形绘制用例。

用例 SHALL 可通过配置选择（既有的 8 帧回归 / 三角形绘制 / 清屏降级），使失败时能快速定位是回归还是新功能问题。

#### Scenario: 回归用例仍可单独运行

- **WHEN** 选择 8 帧回归用例
- **THEN** 输出与本变更前一致，exit 0

#### Scenario: 三角形用例可单独运行

- **WHEN** 选择三角形用例
- **THEN** 渲染并校验中心像素

### Requirement: 适配约束
- 测试代码 SHALL 遵循 `.dsh/rules/cpp.md` 的命名与 include 顺序规范
- SPIR-V 字节数组 SHALL 以 `constexpr` 或 `const` 数组存放，命名遵循 `kPascalCase`
- 测试断言 SHALL 使用既有 `LOG_ASSERT` 或 gtest（`tests/CMakeLists.txt` 已链接 gtest）
- 像素校验的容差 SHALL 定义为具名常量（不用裸字面量）
- 测试 SHALL NOT 依赖有窗口环境——全部经 headless 路径
- 测试运行的运行时环境变量 SHALL 在 tasks 中记录：`DYLD_LIBRARY_PATH=/opt/homebrew/lib` 与 `VK_ICD_FILENAMES=<VulkanSDK>/macOS/share/vulkan/icd.d/MoltenVK_icd.json`

#### Scenario: 编译通过

- **WHEN** 构建整个 `Backend` 静态库
- **THEN** 本能力域的全部源文件编译通过，不依赖 Filament 任何头文件

#### Scenario: 命名与风格合规

- **WHEN** 检查本能力域新增的类型与函数
- **THEN** 命名遵循项目规范（`PascalCase` 类型与公有方法、`m_camelCase` 私有成员、`kPascalCase` 常量），头文件使用 `#pragma once`，不保留上游 license / 文件头注释
