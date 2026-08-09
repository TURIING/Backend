# C++ 规范

## 命名

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
| 文件名 | `PascalCase`，头文件与cpp文件同名即可 | `Header.h` 、 `Header.cpp` |

## 头文件

| 规则 | 说明 |
|------|------|
| 头文件保护 | `#pragma once` |
| Include 顺序 | 本项目 `.h` → 本项目 `""` → 第三方 → C++ 标准库 → C 库，组间空行 |

## 类

| 规则 | 说明 |
|------|------|
| 声明顺序 | `public` → `protected` → `private`，成员变量放最后 |
| 变量暴露方式 | 私有变量供外部使用一律使用`getter`方法获取，而非直接public变量 |

## 现代 C++

| 规则 | 说明 |
|------|------|
| C++标准 | C++20 |
| 空指针 | `nullptr`，不用 `NULL`/`0` |
| 常量 | `constexpr`，不用宏 |
| 枚举 | `enum class`，不用裸 enum |
| 类型别名 | `using`，不用 `typedef` |
| 类型转换 | `static_cast`/`dynamic_cast`，不用 C 风格 |
| 返回值不可忽略 | 函数返回值不应被丢弃时使用 `[[nodiscard]]`（如错误码、句柄、状态查询等） |

## 代码封装抽象
- 魔法数字、重复字符串不散落字面量：定义为 `constexpr` 常量（`kPascalCase`）后复用
  - 反例：`m_capacity = 256;` 与 `new char[256]` 散落，256 含义不明
  - 正例：`constexpr size_t kCapacity = 256;` 定义一次，各处引用 `kCapacity`
- 简短重复的操作（函数/template 无法简洁表达时）定义为宏（`UPPER_SNAKE_CASE`）复用
  - 反例：对齐计算 `(x + 15) & ~15u` 重复写多遍
  - 正例：`#define ALIGN_UP(v, a) (((v) + ((a) - 1)) & ~((a) - 1))` 一处定义、多处调用
- 函数体的执行内容必须与函数名一致：函数只做名称所表达的一件事，不做名外之事
  - 反例：`Update()` 名为"更新"却顺带做了资源加载、日志落盘、网络同步，名不副实
  - 正例：`Update()` 只更新状态，资源加载/日志/网络分别由 `LoadResource()`、`WriteLog()`、`SyncNetwork()` 承担
- 函数体内若存在适合独立成函数的代码段，优先封装成独立函数，函数体只保留编排逻辑
  - 反例：`Initialize()` 从创建实例到创建管线，把整个初始化流程平铺在一个函数里，包含
    实例创建、扩展校验、设备选择、队列族获取、交换链创建、渲染管线构建……每个环节的
    错误处理都混在一起，函数动辄上百行，无法单独复用或测试
  - 正例：`Initialize()` 只负责按序编排，每个环节拆成独立小函数，职责单一、可复用可单测：
    ```
    void Initialize() {
        m_instance   = CreateInstance();
        m_device     = CreateDevice(m_instance);
        m_swapchain  = CreateSwapchain(m_device);
        m_pipeline   = CreatePipeline(m_device);
    }
    ```
    `CreateInstance()` / `CreateDevice()` / `CreateSwapchain()` / `CreatePipeline()`
    各自只做一件事，错误处理内聚在自己内部
