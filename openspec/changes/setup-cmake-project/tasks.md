## 1. Submodule 引入

- [x] 1.1 添加 Utils 为 git submodule：`git submodule add https://github.com/TURIING/Utils 3rd/Utils`
- [x] 1.2 在 `3rd/Utils/` 中创建 `CMakeLists.txt`，定义 `add_library(Utils STATIC)` 并设置 `target_include_directories(Utils PUBLIC include)`
- [x] 1.3 在 `3rd/Utils/` 中创建 `include/Utils/` 和 `src/` 目录（占位）

## 2. 第三方库入口

- [x] 2.1 更新 `3rd/CMakeLists.txt`：`add_subdirectory(Utils)` + 设置 `THIRD_PARTY_LIBS` 变量（`set(... CACHE INTERNAL ...)`）
- [x] 2.1（已废弃）~~创建 `3rd/CMakeLists.txt`，写入 `add_subdirectory(Utils)`~~

## 3. Backend 库骨架

- [x] 3.1 创建 `include/Backend/` 目录（占位）
- [x] 3.2 删除 `src/CMakeLists.txt`（Backend 定义移至顶层 CMakeLists.txt）
- [x] 3.2（已废弃）~~创建 `src/CMakeLists.txt`，定义 `add_library(Backend STATIC)`，链接 Utils，设置 include 路径~~

## 4. 顶层 CMake 配置

- [x] 4.1 更新顶层 `CMakeLists.txt`：`project(Backend)`、C++20、`add_subdirectory(3rd)`，然后直接 `add_library(Backend STATIC src/Backend.cpp)`，`target_link_libraries(Backend PUBLIC ${THIRD_PARTY_LIBS})`

## 5. 验证

- [x] 5.1 执行 `mkdir build && cd build && cmake .. && cmake --build .` 验证构建通过

## 6. GLOB_RECURSE

- [x] 6.1 更新顶层 `CMakeLists.txt`：`file(GLOB_RECURSE BACKEND_SOURCES src/*.cpp)` + `add_library(Backend STATIC ${BACKEND_SOURCES})`
- [x] 6.2 更新 `3rd/Utils/CMakeLists.txt`：`file(GLOB_RECURSE UTILS_SOURCES src/*.cpp)` + `add_library(Utils STATIC ${UTILS_SOURCES})`
- [x] 6.3（Utils 仓库）提交并推送 Utils CMakeLists.txt 变更
- [x] 6.4 重新验证 CMake 构建

## 7. 项目清理与 .gitignore

- [x] 7.1 创建 `.gitignore`，忽略 `build/`、`.DS_Store`、`*.o`、`*.a` 等构建产物和系统文件
- [x] 7.2 从 git 跟踪中移除已误提交的文件（`git rm --cached -r build/`、`git rm --cached .DS_Store`）
- [x] 7.3 提交 `.gitignore` 和清理变更

## 8. 构建产物输出到 bin/

- [x] 8.1 在顶层 `CMakeLists.txt` 中配置 `CMAKE_ARCHIVE_OUTPUT_DIRECTORY`、`CMAKE_LIBRARY_OUTPUT_DIRECTORY`、`CMAKE_RUNTIME_OUTPUT_DIRECTORY` 指向 `${CMAKE_SOURCE_DIR}/bin`
- [x] 8.2 在 `.gitignore` 中添加 `bin/`
- [x] 8.3 重新构建验证产物输出到 `bin/`
- [ ] 8.4 提交变更
