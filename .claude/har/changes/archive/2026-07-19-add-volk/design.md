## Context
项目是一个 C++20 渲染后端抽象库，当前架构使用 `Platform/PlatformFactory` 模式，`BackendType` 枚举已预留 `VULKAN`。项目规则要求第三方库通过 git submodule 获取并存放在 `3rd/` 目录。当前唯一依赖是 `Utils` 子模块。

VOLK 是 Vulkan meta-loader，以 header-only / 单源文件方式分发，支持加载 Vulkan 1.0 到最新扩展。

## Goals / Non-Goals
**Goals:**
- 通过 git submodule 引入 VOLK，编译为 CMake 静态库目标
- Backend 库链接 volk，添加 `VK_NO_PROTOTYPES` 编译定义
- VOLK 仅 Backend 内部使用，公开头文件不暴露

**Non-Goals:**
- 不在此变更中实现 Vulkan Platform 具体逻辑
- 不修改现有 Platform/PlatformFactory 接口
- 不添加 Vulkan SDK 检测（系统上 Vulkan SDK 已安装）

## Decisions

### Decision 1: volk 作为独立 CMake 静态库目标
在 `3rd/CMakeLists.txt` 中通过 `add_library(volk STATIC volk/volk.c)` 创建目标，而非直接在 Backend 中编译 `volk.c`。

**理由：**
- 职责分离：第三方代码归 `3rd/` 管理，与项目源码隔离
- 可复用：如果未来有其他模块需要直接使用 volk，无需重复编译
- 符合 `Utils` 子模块的现有模式

**替代方案：** 直接在 `src/` 下编译 `volk.c`。更简单但打破项目结构约定。

### Decision 2: VK_NO_PROTOTYPES 定义为 PUBLIC
在 `CMakeLists.txt` 对 Backend 目标使用 `target_compile_definitions(Backend PUBLIC VK_NO_PROTOTYPES)`。

**理由：**
- Vulkan 头文件在所有编译单元中行为一致
- PUBLIC 确保任何链接 Backend 的代码也获得此定义

### Decision 3: volk.h 仅在 .cpp 中包含
`#include <volk.h>` 仅限于 Backend 的 `.cpp` 实现文件，不在任何公开头文件中出现。

**理由：**
- 符合方案A — VOLK 对上层透明
- 保持头文件依赖干净

## Risks / Trade-offs
- **MoltenVK 兼容性**：VOLK 支持 MoltenVK，macOS 上 `dlopen("libMoltenVK.dylib")` 可正常加载。无需额外处理。
- **volk 初始化顺序**：`volkInitialize()` 必须在任何 Vulkan 调用前执行。后续实现 Vulkan Platform 时需要注意初始化时机。本次变更仅引入 volk，不调用初始化 — 在后续变更中处理。

## Open Questions
无 — 需求明确，仅引入 volk 依赖。
