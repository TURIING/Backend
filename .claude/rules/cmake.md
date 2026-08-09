# cmake

- 添加代码文件使用 `file(GLOB_RECURSE)` 收集，禁止逐个添加

- 有语义的名称与配置值（项目名、target 名、路径、编译选项、宏定义等）不得硬编码散落：
  - 先用 `set()` 定义为变量，命名遵循 `UPPER_SNAKE_CASE`，再经 `${VAR}` 引用，一处定义、多处复用
  - 同一名称在多个命令中重复出现时，复用同一个变量
  - 同类可扩展的配置（宏定义、依赖列表等）用列表变量统一收集后设置

- 相同或类似的命令聚拢成块
  - 同类命令（多个 `set()`、`add_subdirectory()`、`target_link_libraries()` 等）连续放置、不穿插散落
  - 相似前缀的同族命令（如 `target_*`：`target_include_directories()` / `target_compile_features()` / `target_compile_definitions()` / `target_link_libraries()`）也连续放置、不穿插
  - 聚拢不得破坏依赖顺序（如 `add_subdirectory()` 必须先于其目标的使用）

- CMake 配置按职责拆分，不堆在一个文件里
  - 独立逻辑块（测试目标、子模块构建等）提取到对应目录的 `CMakeLists.txt`，根文件用 `add_subdirectory()` 引入
