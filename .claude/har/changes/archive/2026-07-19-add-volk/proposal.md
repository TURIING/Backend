## Why
项目需要 Vulkan 后端支持，但直接链接系统 Vulkan loader 会引入运行时依赖且加载效率低。引入 VOLK 作为 meta-loader，以静态链接方式封装在 Backend 库内部，实现零运行时依赖的 Vulkan 函数加载。

## What Changes
- 新增 `3rd/volk` git submodule，获取 VOLK 源码
- 修改 `3rd/CMakeLists.txt`，添加 volk 静态库目标
- 修改 `CMakeLists.txt`，为 Backend 库添加 `VK_NO_PROTOTYPES` 编译定义
- Backend 内部通过 volk 加载 Vulkan 函数，上层应用无感知

## Capabilities

### New Capabilities
- `volk-integration`: 通过 VOLK meta-loader 静态链接方式集成 Vulkan 函数加载能力

## Impact
- 受影响的文件：`3rd/CMakeLists.txt`、`CMakeLists.txt`、`.gitmodules`
- 新增：`3rd/volk/`（submodule）
- 构建系统：添加 `VK_NO_PROTOTYPES` 编译定义，新增 volk 库目标
