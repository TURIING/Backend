# Capability: vulkan-resource

## ADDED Requirements

### Requirement: Ref 提供引用归零虚回调

`Utils::Ref` SHALL 在 `SubRef()` 中，当引用计数经 `fetch_sub(1, acq_rel)` 归零时，调用 protected virtual `OnLastRef()`；`OnLastRef()` 默认实现 SHALL 为 `delete this`（保持既有语义）。该改动 SHALL 对既有 Ref 使用者（Driver、VulkanContext、VulInstance 等）行为无影响：`AddRef`/`GetRefCount` 不变，拷贝/移动禁止不变，虚析构不变。

#### Scenario: 默认行为保持

- **WHEN** 既有 Ref 派生对象引用归零，未 override `OnLastRef()`
- **THEN** 走默认 `delete this`，与改动前行为一致

#### Scenario: 派生类接管归零

- **WHEN** 派生类 override `OnLastRef()` 且引用归零
- **THEN** 经虚分发调用派生实现，不执行 `delete this`

### Requirement: Resource 继承 Ref 并携带句柄元数据

`Resource` SHALL 位于 `Backend` 命名空间（`BEGIN_NS_BACKEND`），public 继承 `utils::Ref`，并持有 `ResourceManager*`、`HandleBase::HandleId`、`ResourceType`、销毁标记四个成员；SHALL 提供 `GetId()`/`GetResourceType()` 只读访问（私有成员一律经 getter）。`Resource` SHALL 不自定义计数逻辑：计数、拷贝/移动禁止均继承自 `Ref`。

#### Scenario: 构造初始状态

- **WHEN** 默认构造 `Resource`
- **THEN** `resManager == nullptr`、`id == HandleBase::kNullId`、类型为 `ResourceType::UNDEFINED_TYPE`、销毁标记为 false

#### Scenario: 初始化绑定

- **WHEN** `ResourceManager` 构造对象后调用 `Init<D>(id, rm)`
- **THEN** `id`/`resManager` 被设置，类型经 `GetTypeEnum<D>()` 写入

### Requirement: 引用归零注册延迟销毁

`Resource` SHALL override `OnLastRef()`：经 `resManager->DestructLaterWithType(restype, id)` 将 `{type, id}` 入 GC 队列；对象内存 SHALL 保持不动（由 backend 线程 `gc()` 统一析构并归还 HandleAllocator 池块）。对象 SHALL 经 `Utils::SharedPtr` 持有，`SharedPtr` 自身 SHALL 零改动。

#### Scenario: 最后一个引用释放

- **WHEN** 持有对象的最后一个 `SharedPtr` 释放（析构/移动赋值/`Reset`）且计数归零
- **THEN** `OnLastRef()` 虚分发到 `Resource::OnLastRef`，`{type, id}` 入 GC 队列，对象不被 `delete`，池块未归还

#### Scenario: 归零不直接销毁

- **WHEN** 引用归零后立即检查 HandleAllocator 池块状态
- **THEN** 池块仍被占用，`HandleCast` 仍可解出对象指针

### Requirement: ResourceManager 句柄分配与构造

`ResourceManager` SHALL 包装 `HandleAllocatorVK`（`HandleAllocator<64, 160, 312>`），提供 `AllocHandle<D>()`（仅分配，返回 `Handle<D>`）、`Make<D, B>(handle, args...)`（在既有句柄上构造，返回 `SharedPtr<D>`）、`AllocateAndConstruct<D>(args...)`（分配+构造，返回 `SharedPtr<D>`）。构造 SHALL 经 `HandleAllocator::Construct` 完成 placement-new 后调用 `Init<D>` 绑定元数据。构造函数 SHALL 接受 `arenaSize`、`disableUseAfterFreeCheck`、`disablePoolHandleTags` 三参数透传。

#### Scenario: 分配并构造

- **WHEN** 调用 `AllocateAndConstruct<D>(args...)`
- **THEN** 返回持有有效 id 的 `SharedPtr<D>`，对象位于 HandleAllocator 池内且元数据已绑定

#### Scenario: 既有句柄上构造

- **WHEN** 以 `AllocHandle<D>()` 分配句柄后调用 `Make<D, D>(handle, args...)`
- **THEN** 返回指向该句柄对象的 `SharedPtr<D>`，id 不变

### Requirement: Acquire 语义级 use-after-free 检测

`ResourceManager` SHALL 提供 `Acquire<D, B>(handle)`：经 `HandleAllocator::HandleCast` 解出对象指针，若对象销毁标记已置位 SHALL 以 `LOG_CRITICAL`（含类型与 id 诊断）中止；否则返回 `SharedPtr<D>`。该入口 SHALL 为 driver 层 handle→对象 转换的唯一通道。

#### Scenario: 已销毁句柄再获取

- **WHEN** 对已置销毁标记的句柄调用 `Acquire`
- **THEN** `LOG_CRITICAL` 输出 `used after freed` 诊断并中止

#### Scenario: 正常获取

- **WHEN** 对未销毁的活跃句柄调用 `Acquire`
- **THEN** 返回持有该对象的 `SharedPtr<D>`，引用计数 +1

### Requirement: Destroy 入口与 double-destroy 检测

`ResourceManager` SHALL 提供 `Destroy<D>(SharedPtr<D>&)`：若对象销毁标记已置位 SHALL `LOG_CRITICAL` 报告重复销毁；否则置位销毁标记并 `Reset()` 释放引用（计数归零时按延迟销毁路径入 GC 队列）。

#### Scenario: 重复销毁

- **WHEN** 对同一对象连续两次调用 `Destroy`
- **THEN** 第二次触发 `LOG_CRITICAL` double-destroy 诊断并中止

#### Scenario: 单次销毁

- **WHEN** 对活跃对象调用 `Destroy`
- **THEN** 销毁标记置位、引用释放，对象进入延迟销毁路径

### Requirement: 单 GC 列表与跨线程入队

`ResourceManager` SHALL 以单个 `std::vector<std::pair<ResourceType, HandleId>>` + `std::mutex` 维护 GC 队列（不分裂线程安全/非线程安全两列表）；`DestructLaterWithType` SHALL 持锁 push。入队可发生于任意线程（Ref 计数原子，任意线程归零都可能入队）。

#### Scenario: 并发入队

- **WHEN** 多线程同时归零不同对象的引用
- **THEN** 全部 `{type, id}` 安全入队，无数据竞争

### Requirement: gc 与 terminate 批量销毁

`gc()` SHALL 将 GC 队列 swap 到局部列表（锁内），再逐个 `DestroyWithType(type, id)` 并清空；`terminate()` SHALL 循环 `gc()` 直至队列为空。`DestroyWithType` 当前 SHALL 仅处理 `UNDEFINED_TYPE`（空操作），具体类型分支留待 VulkanHandles 移植补齐（见"类型表骨架"需求）。

#### Scenario: 帧末回收

- **WHEN** 调用 `gc()`
- **THEN** 队列中所有条目经 `DestroyWithType` 处理，队列清空

#### Scenario: 退出回收

- **WHEN** 调用 `terminate()`
- **THEN** 重复 `gc()` 直到 GC 队列为空

### Requirement: 泄漏计数

`ResourceManager` SHALL 在 `BVK_ENABLED(BVK_DEBUG_RESOURCE_LEAK)` 下维护 `uint32_t COUNTER[(size_t)ResourceType::UNDEFINED_TYPE]`：构造对象 +1、`DestroyWithType` 处理 -1；`print()` SHALL 输出各类型存活数与分隔行，`traceConstruction` 在 `UNDEFINED_TYPE` 时断言。

#### Scenario: 计数平衡

- **WHEN** 创建 N 个对象并全部走完销毁路径后调用 `print()`
- **THEN** 各类型计数为 0

#### Scenario: 泄漏可见

- **WHEN** 有对象未销毁时调用 `print()`
- **THEN** 对应类型计数非零并输出

### Requirement: 类型表骨架

`ResourceType` 枚举 SHALL 完整移植 22 种 Filament 类型 + `UNDEFINED_TYPE`（必须为末位，用于枚举迭代）；`GetTypeEnum<D>()` 主模板 SHALL 返回 `UNDEFINED_TYPE`（类型特化留待 VulkanHandles 移植）；`GetTypeStr(ResourceType)` SHALL 完整实现全部枚举值的字符串映射（不依赖类型定义）。枚举值命名与 Filament 一致（UPPER_SNAKE，与项目既有枚举现状一致）。

#### Scenario: 枚举完整性

- **WHEN** 遍历 `ResourceType` 0..UNDEFINED_TYPE-1
- **THEN** 22 种类型全部可枚举，`UNDEFINED_TYPE` 为末位哨兵

#### Scenario: 未实例化不报错

- **WHEN** 仅使用 `GetTypeEnum` 主模板与 `GetTypeStr`
- **THEN** 编译通过，不引用任何 `Vulkan*` 类型定义

### Requirement: 适配约束

`Resource`/`ResourceManager` SHALL 位于 `Backend` 命名空间，复用 `Backend/Handle.h` 的 `HandleBase::HandleId`/`Handle<D>`；API 命名遵循项目规范（PascalCase）；`HandleAllocatorVK` 为 `HandleAllocator<64, 160, 312>`。断言/日志映射为：`assert_invariant` → `LOG_ASSERT`，`FILAMENT_CHECK_PRECONDITION` → 条件失败 `LOG_CRITICAL`（含诊断信息），`LOG(WARNING)` → `LOG_WARN`，`LOG(ERROR)` → `LOG_ERROR`；`FVK_ENABLED(FVK_DEBUG_RESOURCE_LEAK)` → `BVK_ENABLED(BVK_DEBUG_RESOURCE_LEAK)`；`utils::ImmutableCString` → `utils::ImmutableString`；`utils::Mutex` → `std::mutex`。头文件使用 `#pragma once`，include 顺序遵循项目规范（本项目头 → 第三方 Utils → 标准库 → C 库，组间空行）。注释按项目规范重写（不保留 Filament license/文件头注释，不写复述性注释）。

#### Scenario: 编译通过

- **WHEN** 包含 `Resource.h`/`ResourceManager.h` 并链接 `ResourceManager.cpp`
- **THEN** 编译通过，不依赖 Filament 任何头文件与 `resource_ptr`

#### Scenario: 头文件保护与包含

- **WHEN** 检查头文件
- **THEN** 使用 `#pragma once`；include 顺序符合项目规范
