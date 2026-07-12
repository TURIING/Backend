## ADDED Requirements

### Requirement: Git submodule for Utils

Utils 仓库 SHALL 以 git submodule 方式引入，存放于 `3rd/Utils`，并在 `.gitmodules` 中记录。

#### Scenario: Submodule is added
- **WHEN** 执行 `git submodule add https://github.com/TURIING/Utils 3rd/Utils`
- **THEN** `.gitmodules` 文件被创建，且 Utils 被克隆到 `3rd/Utils`

### Requirement: Third-party CMake entry point

`3rd/CMakeLists.txt` SHALL 作为所有第三方库的统一入口，通过 `add_subdirectory` 引入 Utils，并将库 target 收集到 `THIRD_PARTY_LIBS` 变量中。

#### Scenario: Utils is added via 3rd entry point
- **WHEN** CMake 解析 `3rd/CMakeLists.txt`
- **THEN** Utils 子目录被 `add_subdirectory(Utils)` 引入，Utils 被追加到 `THIRD_PARTY_LIBS` 变量

### Requirement: THIRD_PARTY_LIBS variable

`THIRD_PARTY_LIBS` SHALL 作为 CACHE INTERNAL 类型的 CMake 变量，由 `3rd/CMakeLists.txt` 设置，顶层 CMakeLists.txt 在 `target_link_libraries` 中引用。

#### Scenario: Variable is set as CACHE INTERNAL
- **WHEN** `3rd/CMakeLists.txt` 执行 `set(THIRD_PARTY_LIBS ... CACHE INTERNAL ...)`
- **THEN** 顶层 CMakeLists.txt 能通过 `${THIRD_PARTY_LIBS}` 获取第三方 target 列表

#### Scenario: Adding a new third-party lib
- **WHEN** 在 `3rd/` 下新增子模块 `NewLib`
- **THEN** 只需在 `3rd/CMakeLists.txt` 中添加 `add_subdirectory(NewLib)` 并将 `NewLib` 追加到 `THIRD_PARTY_LIBS` 列表，顶层无需修改

### Requirement: Utils static library target

Utils 子模块 SHALL 定义名为 Utils 的静态库目标，通过 `file(GLOB_RECURSE)` 自动收集 `src/` 目录下的源文件，并将 `include/` 目录以 PUBLIC 方式暴露给链接者。

#### Scenario: Utils builds as a static library
- **WHEN** CMake 构建 Utils 目标
- **THEN** 生成静态库（`.a`），且链接 Utils 的 target 自动获得 Utils 的头文件路径

#### Scenario: Utils sources collected via GLOB_RECURSE
- **WHEN** Utils 的 CMakeLists.txt 执行 `file(GLOB_RECURSE UTILS_SOURCES src/*.cpp)`
- **THEN** 所有 Utils `src/` 下的 `.cpp` 文件被自动收集，`add_library(Utils STATIC ${UTILS_SOURCES})` 无需手动维护
