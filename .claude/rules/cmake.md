# cmake

- 添加代码文件使用 `file(GLOB_RECURSE)` 收集，禁止逐个添加

- project name、target name 不得硬编码字符串：
  - 先用 `set()` 定义为变量，命名遵循 `UPPER_SNAKE_CASE`，再经 `${VAR}` 在 `project()` / `add_library()` / `add_executable()` 中引用
  - 同一名称在多处出现时，复用同一个变量
