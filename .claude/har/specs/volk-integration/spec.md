## Purpose
通过 VOLK meta-loader 以静态链接方式为 Backend 库提供 Vulkan 函数加载能力，对上层应用透明。

## Requirements

### Requirement: VOLK 作为 Git Submodule 获取
VOLK 库 SHALL 通过 `git submodule` 方式获取，存放到 `3rd/volk/` 目录。

#### Scenario: 添加 volk submodule
- **WHEN** 执行 `git submodule add https://github.com/zeux/volk.git 3rd/volk`
- **THEN** `.gitmodules` 中应包含 `[submodule "3rd/volk"]` 条目
- **AND** `3rd/volk/` 目录包含 volk 源码（至少 `volk.h` 和 `volk.c`）

### Requirement: VOLK 作为 CMake 静态库目标编译
VOLK SHALL 编译为独立的 CMake 静态库目标（`volk`），供 Backend 库链接。

#### Scenario: volk 编译为静态库
- **WHEN** 执行 CMake 配置和构建
- **THEN** `3rd/volk` 被 `add_subdirectory` 添加，生成 `volk` 静态库目标
- **AND** `volk` 目标编译 `volk.c` 源文件

### Requirement: Backend 库链接 VOLK
Backend 静态库 SHALL 链接 `volk` 目标，并定义 `VK_NO_PROTOTYPES` 宏，使得所有 Vulkan 函数通过 VOLK 加载而非系统 loader。

#### Scenario: Backend 使用 VOLK 加载 Vulkan
- **WHEN** Backend 源码包含 `<volk.h>` 并调用 Vulkan 函数
- **THEN** Vulkan 函数符号通过 volk dispatch table 解析，不依赖系统 `libvulkan` 链接
- **AND** `VK_NO_PROTOTYPES` 宏阻止 Vulkan 头文件声明原型，避免链接冲突

### Requirement: VOLK 对上层透明
Backend 库内部使用 VOLK，但 SHALL NOT 在公开头文件中暴露 volk 依赖。

#### Scenario: 上层应用不感知 VOLK
- **WHEN** 上层应用 `#include <Backend/Common.h>` 或链接 `Backend` 库
- **THEN** 不需要包含 volk 头文件，不需要链接 volk 目标
- **AND** Backend 公开头文件不出现 `volk.h` 引用
