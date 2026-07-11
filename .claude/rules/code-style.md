# 代码编写原则
- 第三方库一律使用`git submodule`的方式获取，并存放在3rd/
- 代码注释规范
  - 每个函数（除了get、set等简单明了的函数外）都应在头文件文件里写上函数注释
  - 需要在头文件里写上类注释
  - 函数体内注释仅在逻辑复杂时写
- 代码编写风格
  - 尽量封装可复用的代码，比如利用宏定义、template等

# 命名

| 种类 | 风格 | 示例 |
|------|------|------|
| 类/结构体 | `PascalCase` | `UserManager` |
| 公有函数/成员函数 | `PascalCase` | `GetName()` |
| 静态变量 | `sPascalCase` | `sGetName()` |
| 变量 | `camelCase` | `userCount` |
| 私有成员变量 | `m_camelCase` | `m_name`、`m_pPointer` |
| 常量 | `kPascalCase` | `kMaxSize` |
| 枚举值 | `PascalCase` | `Red` |
| 命名空间 | `snake_case` | `my_project` |
| 宏 | `UPPER_SNAKE_CASE` | `MY_VERSION` |

# 头文件

| 规则 | 说明 |
|------|------|
| 头文件保护 | `#pragma once` |
| Include 顺序 | 本项目 `.h` → 本项目 `""` → 第三方 → C++ 标准库 → C 库，组间空行 |

# 类

| 规则 | 说明 |
|------|------|
| 声明顺序 | `public` → `protected` → `private`，成员变量放最后 |
| 变量暴露方式 | 私有变量供外部使用一律使用`getter`方法获取，而非直接public变量 |

# 现代 C++

| 规则 | 说明 |
|------|------|
| C++标准 | C++20 |
| 空指针 | `nullptr`，不用 `NULL`/`0` |
| 常量 | `constexpr`，不用宏 |
| 枚举 | `enum class`，不用裸 enum |
| 类型别名 | `using`，不用 `typedef` |
| 类型转换 | `static_cast`/`dynamic_cast`，不用 C 风格 |

# cmake
- 添加代码文件要使用`file GLOB_RECURSE`的方式，不能一个一个添加