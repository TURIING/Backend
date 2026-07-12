## Why

Backend 项目目前是一个空仓库，需要建立 CMake C++20 构建体系，并引入 Utils 作为首个第三方依赖，为后续开发提供基础骨架。

## What Changes

- 创建顶层 CMakeLists.txt，配置 C++20 标准、项目声明，直接定义 Backend 静态库
- 添加 `https://github.com/TURIING/Utils` 作为 git submodule，存放于 `3rd/Utils`
- 创建 `3rd/CMakeLists.txt` 作为第三方库统一管理入口，通过 `THIRD_PARTY_LIBS` 变量收集库 target 并传递给顶层
- 在 Utils 子模块中创建最小 CMake 骨架（空静态库）
- 创建 Backend 静态库目标，链接 Utils
- 创建 `include/Backend/` 公共头文件目录

## Capabilities

### New Capabilities
- `cmake-build-system`: CMake C++20 构建系统，包含项目声明、编译选项、多级目录结构
- `third-party-dependency-management`: 基于 git submodule + CMake add_subdirectory 的第三方依赖管理

### Modified Capabilities
<!-- 当前无已有 capability -->

## Impact

- 项目根目录新增 `CMakeLists.txt`、`.gitmodules`、`.gitignore`
- 新建 `3rd/`、`src/`、`include/Backend/` 目录；`src/` 仅存放源文件，无独立 CMakeLists.txt
