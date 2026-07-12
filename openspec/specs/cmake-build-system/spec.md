# CMake Build System

## Purpose

为 Backend 项目提供 CMake C++20 构建系统，包括项目声明、编译选项、多级目录结构，并确保构建产物输出到统一目录。

## Requirements

### Requirement: CMake C++20 project configuration

顶层 CMakeLists.txt SHALL 声明项目名称为 Backend，使用 C++20 标准，并要求编译器强制支持。

#### Scenario: Project is configured with C++20
- **WHEN** 执行 `cmake ..` 配置项目
- **THEN** CMake 使用 C++20 标准，且 `CMAKE_CXX_STANDARD_REQUIRED` 为 ON

### Requirement: Build dependency order

顶层 CMakeLists.txt SHALL 先通过 `add_subdirectory(3rd)` 构建第三方库，再定义 Backend 库并链接由 `THIRD_PARTY_LIBS` 变量收集的第三方 target。

#### Scenario: Third-party libs built before Backend
- **WHEN** CMake 解析顶层 CMakeLists.txt
- **THEN** 先 `add_subdirectory(3rd)` 构建 Utils，Backend 库随后定义为 `add_library(Backend STATIC)`，并通过 `${THIRD_PARTY_LIBS}` 链接

### Requirement: Backend static library target

顶层 CMakeLists.txt SHALL 直接定义一个名为 Backend 的静态库目标，通过 `file(GLOB_RECURSE)` 自动收集 `src/` 目录下的源文件。

#### Scenario: Backend links Utils as PUBLIC
- **WHEN** Backend 库被构建
- **THEN** Backend 以 PUBLIC 方式链接 `${THIRD_PARTY_LIBS}` 变量中的第三方库，且 `target_include_directories` 指向 `${PROJECT_SOURCE_DIR}/include`

#### Scenario: Sources collected via GLOB_RECURSE
- **WHEN** 顶层 CMakeLists.txt 执行 `file(GLOB_RECURSE BACKEND_SOURCES src/*.cpp)`
- **THEN** 所有 `src/` 下的 `.cpp` 文件被自动收集到 `BACKEND_SOURCES` 变量，`add_library(Backend STATIC ${BACKEND_SOURCES})` 无需手动列出源文件

#### Scenario: No per-directory CMakeLists under src
- **WHEN** 浏览 `src/` 目录
- **THEN** 该目录仅包含源文件，不存在独立 CMakeLists.txt
