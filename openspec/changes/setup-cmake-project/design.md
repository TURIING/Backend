## Context

Backend 是从零开始的项目，当前仓库仅有 `.claude/` 和 `openspec/` 配置文件。需要建立 CMake C++20 构建体系，并通过 git submodule 引入 Utils 作为静态库依赖。

## Goals / Non-Goals

**Goals:**
- 搭建可扩展的 CMake 构建系统（顶层直接定义 Backend 库）
- Utils 作为 git submodule 管理，版本可控
- Utils 编译为静态库，Backend 以 PUBLIC 方式链接
- 3rd/CMakeLists.txt 作为第三方库统一入口，通过 `THIRD_PARTY_LIBS` 变量向顶层传递库 target 列表，方便未来扩展
- Backend 本身也是一个静态库

**Non-Goals:**
- 不涉及 CI/CD 配置
- 不添加除 Utils 以外的第三方库
- 不创建可执行目标（Backend 是库，不是可执行文件）
- 不涉及测试框架配置

## Decisions

### 1. 第三方库管理方式：git submodule + add_subdirectory

| 方案 | 描述 | 评估 |
|------|------|------|
| git submodule + add_subdirectory | 直接将第三方源码作为子目录编译 | ✅ 选中 |
| FetchContent | CMake 内置的源码下载 | ❌ 网络依赖，离线不可用 |
| find_package + 系统安装 | 要求系统预装库 | ❌ 不可移植 |
| CPM.cmake | 第三方包管理器 | ❌ 引入额外依赖，不符合项目规范 |

**选择理由**: 项目规范要求第三方库使用 git submodule；submodule 版本锁定在 git commit，可精确复现；不需要外部包管理器。

### 2. 3rd/ 层级结构与变量传递

```
3rd/
├── CMakeLists.txt          ← 统一入口，add_subdirectory + 收集 target
└── Utils/                  ← submodule
    ├── CMakeLists.txt
    └── ...
```

每个第三方库自包含其 CMake 配置，3rd/CMakeLists.txt 负责遍历引入。使用 `THIRD_PARTY_LIBS` 变量（CACHE INTERNAL）收集所有第三方库 target，供顶层 `target_link_libraries` 引用。

```cmake
# 3rd/CMakeLists.txt
add_subdirectory(Utils)
set(THIRD_PARTY_LIBS
    Utils
    CACHE INTERNAL "Third-party library targets"
)
```

选择 CACHE INTERNAL 而非 PARENT_SCOPE 的原因：变量需要跨 `add_subdirectory` 边界从 3rd 传递到顶层。PARENT_SCOPE 只影响直接父级，CACHE INTERNAL 在整个项目中可见且不会被外部覆盖。

### 3. Backend 库定义位置：顶层 CMakeLists.txt

Backend 库定义放在顶层 CMakeLists.txt 而非 `src/CMakeLists.txt`：

```cmake
# 顶层 CMakeLists.txt
add_subdirectory(3rd)                          # 先构建第三方，产生 THIRD_PARTY_LIBS

file(GLOB_RECURSE BACKEND_SOURCES src/*.cpp)   # 自动收集 src/ 下所有 .cpp
add_library(Backend STATIC ${BACKEND_SOURCES}) # 直接定义 Backend 库
target_include_directories(Backend PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(Backend PUBLIC ${THIRD_PARTY_LIBS})
```

| 方案 | 描述 | 评估 |
|------|------|------|
| 顶层直接定义 | Backend 库与顶层配置在同一文件 | ✅ 选中 |
| src/CMakeLists.txt | 单独文件定义 Backend | ❌ 多一层间接引用，无实际收益 |

**选择理由**: 当前项目规模小，Backend 源文件集中在 `src/` 目录下。顶层直接定义避免了 `add_subdirectory(src)` 的额外间接层，且 `THIRD_PARTY_LIBS` 变量无需跨多层传递。未来若 `src/` 下出现复杂子目录结构，再考虑拆分。

### 4. link 可见性：PUBLIC

```cmake
target_link_libraries(Backend PUBLIC Utils)
```

选择 PUBLIC 而非 PRIVATE 的原因：如果未来有项目链接 Backend，Utils 的头文件路径能自动传递，不需要消费者手动 `find_package` Utils。

### 5. Utils 最小骨架

Utils 仓库当前为空，需要在本地创建最小 CMake 骨架：

```cmake
file(GLOB_RECURSE UTILS_SOURCES src/*.cpp)
add_library(Utils STATIC ${UTILS_SOURCES})
target_include_directories(Utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
```

选择最小骨架而非直接让 Backend 包装 Utils 的原因：Utils 是独立仓库，应由 Utils 自己管理构建逻辑，Backend 只负责引入。

### 6. 源文件收集方式：file(GLOB_RECURSE)

源文件使用 `file(GLOB_RECURSE)` 自动收集，而非逐个显式列出：

```cmake
file(GLOB_RECURSE BACKEND_SOURCES src/*.cpp)
add_library(Backend STATIC ${BACKEND_SOURCES})
```

| 方案 | 评估 |
|------|------|
| `file(GLOB_RECURSE)` 自动收集 | ✅ 选中，添加文件无需改 CMakeLists.txt |
| 手动逐个列出 | ❌ 频繁改 CMakeLists.txt，样板代码多 |

**选择理由**: 项目处于早期阶段，源文件变动频繁。GLOB_RECURSE 避免每次增删文件都修改 CMakeLists.txt。缺点是新增文件后需 re-run cmake configure（`cmake --build` 不会自动检测新文件），这在当前项目规模下完全可接受。

### 7. 顶层目录结构

```
Backend/
├── CMakeLists.txt          ← project(), C++20, add_subdirectory(3rd), add_library(Backend)
├── 3rd/
│   ├── CMakeLists.txt      ← add_subdirectory + THIRD_PARTY_LIBS 变量
│   └── Utils/ (submodule)
├── include/
│   └── Backend/            ← Backend 公共头文件
└── src/
    └── Backend.cpp         ← Backend 源文件（无独立 CMakeLists.txt）
```

顶层 CMakeLists.txt 承担项目配置、依赖调度、Backend 库定义三合一角色。src/ 目录仅存放源文件，不包含 CMakeLists.txt。依赖关系清晰：3rd 构建第三方库并通过变量传递 target 列表，顶层将变量注入 Backend 的链接依赖。

## Risks / Trade-offs

- **[submodule 更新遗忘]** → 其他开发者 clone 后需执行 `git submodule update --init`。已在 tasks 中记录此操作。
- **[Utils 仓库为空导致编译空库]** → CMake STATIC 库可以在无源文件的空状态下正常通过 configure，不产生编译错误，但也不产生实质性 .a 文件。后续 Utils 添加源文件后自然解决。
- **[目录尚不存在时 CMake 可能报错]** → 3rd/ 和 src/ 目录需在 cmake configure 前创建到位（src/ 需包含 Backend.cpp）。

## Open Questions

无 — 所有决策已在探索阶段确认。
