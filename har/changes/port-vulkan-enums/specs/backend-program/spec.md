# Capability: backend-program

## Purpose

`Program` 是着色器二进制的容器与管线创建的输入契约。它由前端（app 侧）构造，经 `createProgram` / `createProgramAsync` 传入后端；后端据此创建 `VkShaderModule` 与 `VkPipeline`。

上游对应物为 `backend/include/backend/Program.h` + `backend/src/Program.cpp`（114 行）。本能力域只移植**数据载体与访问器**，不涉及任何着色器编译——GLSL → SPIR-V 由前端完成，后端只接收二进制。

## ADDED Requirements
### Requirement: Program 类型与生命周期

`include/Backend/Program.h` SHALL 定义 `Backend::Program`（非虚、不可拷贝、可移动），配套 `src/Program.cpp`：

- `Program() noexcept` 默认构造
- 删除拷贝构造与拷贝赋值
- 移动构造与移动赋值 `= default`
- `~Program() noexcept = default`

构造与析构 SHALL 定义在 `.cpp` 中（上游注释说明「so they're not inlined」），符合项目「头文件只放声明」的分层习惯。

#### Scenario: 不可拷贝

- **WHEN** 尝试 `Program b(a);`
- **THEN** 编译失败（拷贝构造已删除）

#### Scenario: 可移动

- **WHEN** `Program b(std::move(a));`
- **THEN** 编译通过，`b` 持有原数据，`a` 处于有效但未指定状态

### Requirement: 链式配置接口

`Program` SHALL 提供以下链式 setter（返回 `Program&`），字段名与语义逐一对齐上游：

- `PriorityQueue(CompilerPriorityQueue)`
- `ShaderLanguage(ShaderLanguage)`
- `Shader(ShaderStage, void const* data, size_t size)`：按 `ShaderStage` 下标写入着色器二进制
- `DescriptorBindings(descriptor_set_t, DescriptorBindingsInfo)`
- `DescriptorLayout(descriptor_set_t, DescriptorSetLayout)`：追加一条 descriptor set 布局
- `Uniforms(uint32_t index, NS_UTILS::String name, UniformInfo)`
- `Attributes(AttributesInfo)`
- `SpecializationConstants(SpecializationConstantsInfo)`
- `PushConstants(ShaderStage, std::vector<PushConstant>)`
- `CacheId(uint64_t)`
- `Multiview(bool)`

命名 SHALL 按项目规范改为 `PascalCase`（上游为 `camelCase`）。`Shader` SHALL 以 `std::copy_n` 从 `void const*` 拷贝 `size` 字节。

`Diagnostics(...)` SHALL NOT 移植——它的唯一作用是持有 `io::ostream` 日志回调，项目用 spdlog，该回调无消费者。

#### Scenario: 链式构造

- **WHEN** `program.PriorityQueue(CompilerPriorityQueue::HIGH).ShaderLanguage(ShaderLanguage::SPIRV).Shader(ShaderStage::VERTEX, data, size)`
- **THEN** 三次调用均返回 `Program&`，最终对象持有对应值

#### Scenario: 着色器按 stage 下标存放

- **WHEN** 以 `ShaderStage::VERTEX` 与 `ShaderStage::FRAGMENT` 分别写入二进制后调用 `GetShadersSource()`
- **THEN** 两个 blob 分别位于下标 0 与 1（`static_cast<size_t>(stage)`）

#### Scenario: 无 ostream 依赖

- **WHEN** include `Backend/Program.h`
- **THEN** 不引入 `utils/io/ostream` 相关符号，`mLogger` 成员不存在

### Requirement: 嵌套类型与视图

`Program` SHALL 定义以下嵌套类型（`FixedCapacityVector` → `std::vector` 映射）：

| 上游 | 本项目 |
|---|---|
| `using ShaderBlob = utils::FixedCapacityVector<uint8_t>;` | `using ShaderBlob = std::vector<uint8_t>;` |
| `using ShaderSource = std::array<ShaderBlob, SHADER_TYPE_COUNT>;` | 同构 |
| `using SpecializationConstant = std::variant<int32_t, float, bool>;` | 同构 |
| `using DescriptorSetLayoutArray = utils::FixedCapacityVector<DescriptorSetLayoutBinding>;` | `using DescriptorSetLayoutArray = std::vector<DescriptorSetLayoutBinding>;` |
| `using DescriptorBindingsInfo` / `DescriptorSetInfo` / `SpecializationConstantsInfo` / `AttributesInfo` / `UniformInfo` / `BindingUniformsInfo` | 同构（容器换 `std::vector`） |
| `struct Descriptor` / `DescriptorSetLayoutBinding` / `Uniform` / `PushConstant` | 逐字段对齐 |

容量常量 `SHADER_TYPE_COUNT = 3` / `UNIFORM_BINDING_COUNT` / `SAMPLER_BINDING_COUNT` SHALL 落在 `Program` 类内（按上游）。`CONFIG_UNIFORM_BINDING_COUNT` / `CONFIG_SAMPLER_BINDING_COUNT` 为上游 CMake 注入的构建期配置，本项目 SHALL 以 `constexpr` 常量在 `Program.h` 内定义（不再引入 CMake 宏定义选项）。

`Program::SAMPLER_BINDING_COUNT` 被 `VulkanDriver` 用作 `MAX_SAMPLER_BINDING_COUNT`，其值 SHALL 与上游一致。

#### Scenario: 嵌套类型可用

- **WHEN** 声明 `Program::ShaderSource src;` 与 `Program::DescriptorBindingsInfo info;`
- **THEN** 编译通过

#### Scenario: 容量常量可被下游引用

- **WHEN** 在 `VulkanDriver.h` 写 `static constexpr uint8_t MAX_SAMPLER_BINDING_COUNT = Program::SAMPLER_BINDING_COUNT;`
- **THEN** 编译通过

### Requirement: 只读访问器

`Program` SHALL 提供以下访问器（上游同名，`PascalCase` 化）：

- `GetShadersSource()` / `GetName()` / `GetShaderLanguage()` / `GetCacheId()` / `IsMultiview()` / `GetPriorityQueue()`
- `GetSpecializationConstants()` / `GetDescriptorBindings()` / `GetDescriptorSetLayouts()`
- `GetPushConstants(ShaderStage)`：常量与非常量两个重载
- `GetBindingUniformInfo()` / `GetAttributes()`：常量与非常量两个重载

`GetDescriptorSetLayouts()` SHALL 返回 `DescriptorSetLayoutArray const&`——上游 `createProgramR` 遍历它以创建 `VulkanDescriptorSetLayout`，返回类型不得偏离。

#### Scenario: 布局列表可遍历

- **WHEN** 对 `program.GetDescriptorSetLayouts()` 做范围 for，读取每项的 `.set` 与 `.layout`
- **THEN** 编译通过，语义与上游 `createProgramR` 的循环一致

### Requirement: 适配约束
- `Program.h` SHALL 位于 `include/Backend/`（公共头，前端需构造它）
- `Program.cpp` SHALL 位于 `src/`
- 命名 SHALL 遵循项目规范：类型 `PascalCase`、公有函数 `PascalCase`、成员变量 `m_camelCase`、常量 `kPascalCase`
- `Program::DescriptorSetLayoutBinding`（内部类型）与 `Backend::DescriptorSetLayout`（`DriverDefine.h` 的公共类型）SHALL 保持**不同的完整限定名**。上游二者同名但位于不同作用域，移植时须显式确认每个引用点解析到正确类型，否则变更 6 的 `createProgramR` 会静默使用错误的类型
- 类内声明 SHALL 遵循 `.dsh/rules/cpp.md` 的空行分组：`public` 区按「嵌套类型 → 特殊成员函数 → 链式 setter → 访问器」分组，组间空行，组内连续；成员变量统一置末
- 注释 SHALL 遵循 `.dsh/rules/code-style.md`：上游注释（如 `// For ES2 support`）须按本规范重写或删除，只保留解释意图的
- 本能力域 SHALL NOT 提供 GLSL → SPIR-V 编译能力，SHALL NOT 引入 `glslang` 等依赖

#### Scenario: 编译通过

- **WHEN** 构建整个 `Backend` 静态库
- **THEN** 本能力域的全部源文件编译通过，不依赖 Filament 任何头文件

#### Scenario: 命名与风格合规

- **WHEN** 检查本能力域新增的类型与函数
- **THEN** 命名遵循项目规范（`PascalCase` 类型与公有方法、`m_camelCase` 私有成员、`kPascalCase` 常量），头文件使用 `#pragma once`，不保留上游 license / 文件头注释
