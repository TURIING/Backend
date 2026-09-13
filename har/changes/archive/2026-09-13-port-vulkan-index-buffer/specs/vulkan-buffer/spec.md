## ADDED Requirements

### Requirement: HwIndexBuffer 资源结构

`HwIndexBuffer` SHALL 定义于 `src/HwDefine.h`（`Hw*` 资源结构统一定义处），public 继承 `HwBase`，以位域承载 `count` / `elementSize` / `asynchronous` 三字段，总宽 32 位。位宽 SHALL 以文件内 `constexpr` 常量表达（`kIndexCountBits = 26`、`kElementSizeBits = 5`），不散落字面量。SHALL 提供默认构造与 `(uint8_t elementSize, uint32_t indexCount, bool async)` 构造；后者 SHALL 以 `LOG_ASSERT` 约束 `elementSize ∈ [1, 16]` 与 `indexCount < 2^26`。

#### Scenario: 位域字段写入

- **WHEN** 以 `elementSize = 4`、`indexCount = 1024`、`async = false` 构造 `HwIndexBuffer`
- **THEN** `count` 为 1024、`elementSize` 为 4、`asynchronous` 为 false

#### Scenario: 越界构造断言

- **WHEN** 以 `elementSize = 0`、`elementSize = 17` 或 `indexCount >= 2^26` 构造（debug 构建）
- **THEN** 触发 `LOG_ASSERT`

#### Scenario: 默认构造零初始化

- **WHEN** 默认构造 `HwIndexBuffer`
- **THEN** `count` / `elementSize` / `asynchronous` 均为 0

### Requirement: VulkanIndexBuffer 资源对象

`VulkanIndexBuffer` SHALL 定义于 `src/vulkan/VulkanHandle.h/.cpp`，public 继承 `HwIndexBuffer` 与 `Resource`，构造签名 SHALL 为 `(const VulkanContextPtr&, VmaAllocator, const VulkanBufferCachePtr&, uint8_t elementSize, uint32_t indexCount)`（不含 `VulkanStagePool` 参数，与本地 `VulkanBufferProxy` 形态一致）。SHALL 组合私有成员 `VulkanBufferProxy m_buffer`，以 `VulkanBufferBinding::Index`、`BufferUsage::STATIC`、字节数 `elementSize * indexCount` 构造，并提供 `NODISCARD VkBuffer GetVkBuffer() const noexcept` 透传底层句柄。SHALL 提供公开常量成员 `VkIndexType const indexType`：`elementSize == sizeof(uint16_t)` 时为 `VK_INDEX_TYPE_UINT16`，否则为 `VK_INDEX_TYPE_UINT32`。SHALL 以 `DECLARE_SHARE_PTR_CLASS` 声明 `VulkanIndexBufferPtr`。

#### Scenario: 32 位索引推导

- **WHEN** 以 `elementSize = 4`、`indexCount = n` 构造
- **THEN** `indexType` 为 `VK_INDEX_TYPE_UINT32`，底层 `VulkanBufferProxy` 的 binding 为 `Index`，`GetVkBuffer()` 返回非空 `VkBuffer`

#### Scenario: 16 位索引推导

- **WHEN** 以 `elementSize = 2` 构造
- **THEN** `indexType` 为 `VK_INDEX_TYPE_UINT16`

#### Scenario: 基类字段继承

- **WHEN** 构造后读取基类字段
- **THEN** `count` 等于构造入参 `indexCount`，`asynchronous` 为 false

### Requirement: 索引缓冲驱动接线

`VulkanDriver` SHALL 实现三个方法：

- `CreateIndexBufferS() noexcept`：返回 `m_resMgr->AllocHandle<VulkanIndexBuffer>()`
- `CreateIndexBufferR(Handle<HwIndexBuffer> ibh, ElementType elementType, uint32_t indexCount, BufferUsage usage, NS_UTILS::ImmutableString&& tag)`：经 `Driver::GetElementTypeSize(elementType)` 求得 `uint8_t` 元素宽度后 `Make<VulkanIndexBuffer>`，并 `AssociateTagToHandle(ibh.GetId(), std::move(tag))`
- `DestroyIndexBuffer(IndexBufferHandle ibh)`：空句柄直接返回；否则 `Acquire<VulkanIndexBuffer>` 后 `Destroy`

#### Scenario: 创建后销毁

- **WHEN** 经命令流创建索引缓冲再销毁
- **THEN** 资源对象构造一次、句柄与 tag 关联、`Destroy` 置销毁标记并释放引用，全过程无 `LOG_CRITICAL`

#### Scenario: 空句柄销毁

- **WHEN** 以空 `IndexBufferHandle` 调用 `DestroyIndexBuffer`
- **THEN** 直接返回，不触碰资源层

#### Scenario: 元素宽度换算贯通

- **WHEN** 以 `ElementType::USHORT` 创建索引缓冲
- **THEN** 对象 `elementSize` 为 2、`indexType` 为 `VK_INDEX_TYPE_UINT16`

### Requirement: 索引缓冲适配约束

索引缓冲各类型 SHALL 位于 `Backend` 命名空间（`BEGIN_NS_BACKEND`）；引用计数与持有经 `NS_UTILS::SharedPtr` + `Resource`；`assert_invariant` → `LOG_ASSERT`、`PANIC_LOG` → `LOG_CRITICAL`；命名遵循项目规范（`PascalCase` 方法、`m_camelCase` 私有成员、`kPascalCase` 常量）；头文件 `#pragma once`，不保留上游 license / 文件头注释。

#### Scenario: 编译通过

- **WHEN** 构建整个 `Backend` 静态库
- **THEN** `src/vulkan/VulkanHandle.cpp` / `VulkanDriver.cpp` 编译通过，不依赖 Filament 任何头文件

#### Scenario: 命名与既有类型一致

- **WHEN** 检查 `VulkanIndexBuffer` 定义
- **THEN** 成员 `m_buffer` 为私有、`indexType` 为公开常量、访问器为 `GetVkBuffer()`
