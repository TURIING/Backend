# Capability: vulkan-buffer

## Purpose

VkBuffer 的资源化与缓存池：`VulkanBuffer` 是接入 `vulkan-resource` 生命周期层的 GPU buffer 资源对象，`VulkanBufferCache` 按 binding 分池、以 LRU 复用与淘汰 VkBuffer，`VulkanBufferProxy` 对外提供解耦的动态代理。VMA（VulkanMemoryAllocator）作为分配后端，经独立编译单元接入并配合 volk 动态加载。

## Requirements

### Requirement: VMA 依赖接入

VMA（VulkanMemoryAllocator）SHALL 以 `git submodule` 方式存放于 `3rd/VulkanMemoryAllocator`，CMake 侧 SHALL 以 INTERFACE target（`vma.cmake`，仿照 `volk.cmake`）暴露 `vk_mem_alloc.h` 的 include 路径，并接入 `3rd/CMakeLists.txt` 的 `THIRD_PARTY_LIBS`。SHALL 提供独立编译单元 `src/vulkan/VmaImpl.cpp`：定义 `VMA_IMPLEMENTATION`，编译期配置 `VMA_STATIC_VULKAN_FUNCTIONS=0`、`VMA_DYNAMIC_VULKAN_FUNCTIONS=1`（Vulkan 函数经 volk 动态加载）。不得依赖 Filament 任何头文件。

#### Scenario: 编译与链接

- **WHEN** 全量构建 Backend target
- **THEN** `vmaCreateBuffer`/`vmaDestroyBuffer`/`vmaFlushAllocation` 等符号解析成功，无 VMA 相关编译错误

#### Scenario: 动态函数配置

- **WHEN** 检查 `VmaImpl.cpp` 的宏配置
- **THEN** `VMA_STATIC_VULKAN_FUNCTIONS` 为 0、`VMA_DYNAMIC_VULKAN_FUNCTIONS` 为 1，与 volk 动态加载架构一致

### Requirement: VulkanBufferBinding 与 VulkanGpuBuffer

`VulkanBufferBinding` 枚举 SHALL 完整移植（`UNKNOWN`/`VERTEX`/`INDEX`/`UNIFORM`/`SHADER_STORAGE`）；`VulkanGpuBuffer` 结构 SHALL 携带 `VkBuffer`、`VmaAllocation`、`VmaAllocationInfo`、`numBytes`、`binding` 五个字段。二者 SHALL 位于 `Backend` 命名空间、`src/vulkan/buffer/VulkanBuffer.h`（buffer 功能域私有定义，不越层）。

#### Scenario: 字段完整

- **WHEN** 构造 `VulkanGpuBuffer`
- **THEN** `vkbuffer`/`vmaAllocation` 为 `VK_NULL_HANDLE`，`allocationInfo` 置零，`numBytes=0`，`binding=UNKNOWN`

### Requirement: BufferUsage 枚举

`BufferUsage` 枚举 SHALL 移植到 `include/Backend/DriverDefine.h`（driver 通用定义文件，供多 backend 复用）：`STATIC=0`、`DYNAMIC=1`、`DYNAMIC_BIT=0x1`、`SHARED_WRITE_BIT=0x04`。本次 SHALL 仅作构造参数与成员存储（`loadFromCpu` 未移植，位运算符与 `any()` 用法不在本次范围）。

#### Scenario: 枚举值一致

- **WHEN** 引用 `BufferUsage::STATIC` / `DYNAMIC_BIT` / `SHARED_WRITE_BIT`
- **THEN** 数值与原 Filament `backend/DriverEnums.h` 一致

### Requirement: VulkanContext 适配访问器

`VulkanContext` SHALL 提供 `stagingBufferBypassEnabled()` 只读访问器返回 `m_stagingBufferBypassEnabled`（成员已由 VulkanPlatform 写入），支撑 VulkanBufferProxy 保留原构造签名。

#### Scenario: 读取开关

- **WHEN** 调用 `context.stagingBufferBypassEnabled()`
- **THEN** 返回 `m_stagingBufferBypassEnabled` 当前值

### Requirement: VulkanBuffer 资源对象

`VulkanBuffer` SHALL public 继承 `Resource`（经 `ResourceManager::AllocateAndConstruct` 池化分配），持有 `VulkanGpuBuffer const*` 与 `OnRecycle` 回调（`std::function<void(VulkanGpuBuffer const*)>`）；析构 SHALL 调用 `OnRecycleFn` 将 gpuBuffer 归还其所属缓存池；`GetGpuBuffer()` SHALL 为唯一只读访问通道。类 SHALL 位于 `src/vulkan/buffer/VulkanBuffer.h`。

#### Scenario: 析构归还

- **WHEN** `VulkanBuffer` 经 GC 路径析构（`~VulkanBuffer` 执行）
- **THEN** `OnRecycleFn` 被调用且参数为构造时绑定的 `VulkanGpuBuffer const*`

#### Scenario: 只读访问

- **WHEN** 外部需要访问 GPU buffer 元数据
- **THEN** 仅经 `GetGpuBuffer()` 获取，成员私有

### Requirement: VulkanBufferCache 缓存池

`VulkanBufferCache` SHALL 提供构造 `(VulkanContext const&, ResourceManager&, VmaAllocator)`；`Acquire(binding, numBytes)` 返回 `NS_UTILS::SharedPtr<VulkanBuffer>`：先对 binding 对应池 `lower_bound(numBytes)` 复用不小于请求大小的空闲 gpuBuffer，未命中则 `Allocate` 新建，二者均经 `m_resourceManager.AllocateAndConstruct<VulkanBuffer>(gpuBuffer, onRecycle)` 构造并绑定 `Release` 回调；`Gc()` SHALL 帧计数 `++` 后在前 3 帧提前返回（避免无符号回绕），之后按 `lastAccessed < currentFrame - 3` 淘汰并 `Destroy`；`Terminate()` SHALL 销毁全部池内缓冲并清池。私有 `Release` 按 `binding`/`numBytes` 回插池、`Allocate` 以 `getVkBufferUsage(binding) | VK_BUFFER_USAGE_TRANSFER_DST_BIT` 且 UMA 下追加 `VMA_ALLOCATION_CREATE_MAPPED_BIT | HOST_ACCESS_SEQUENTIAL_WRITE_BIT` 经 `vmaCreateBuffer` 创建、`Destroy` 经 `vmaDestroyBuffer` + `delete`。四个 binding 各占一个池（`MAX_POOL_COUNT=4`），`UNKNOWN` 池访问 SHALL `LOG_CRITICAL` 中止。不可拷贝。类 SHALL 位于 `src/vulkan/buffer/VulkanBufferCache.h/.cpp`。

#### Scenario: 池内复用

- **WHEN** 连续 `Acquire(UNIFORM, n)` 后释放，再 `Acquire(UNIFORM, m)` 且 `m <= n`
- **THEN** 第二次 Acquire 复用同一 `VulkanGpuBuffer`（不新建 VkBuffer），`numBytes` 不小于请求值

#### Scenario: 池外新建

- **WHEN** `Acquire` 请求大小大于池内所有空闲缓冲
- **THEN** 经 `Allocate` 新建 VkBuffer，usage 含 `VK_BUFFER_USAGE_TRANSFER_DST_BIT`，UMA 下 allocationInfo.pMappedData 非空

#### Scenario: LRU 淘汰

- **WHEN** `Gc()` 累计调用超过 3 次，存在 `lastAccessed < currentFrame - 3` 的空闲缓冲
- **THEN** 该缓冲经 `Destroy` 销毁并从池中移除；`BVK_ENABLED(BVK_DEBUG_VULKAN_BUFFER_CACHE)` 下输出销毁日志

#### Scenario: 终止清空

- **WHEN** 调用 `Terminate()`
- **THEN** 全部池内 gpuBuffer 经 `vmaDestroyBuffer` 销毁、池清空

#### Scenario: 未知 binding 防御

- **WHEN** `UNKNOWN` binding 请求进入 `GetPool`
- **THEN** `LOG_CRITICAL` 中止

### Requirement: VulkanBufferProxy 动态代理

`VulkanBufferProxy` SHALL 保留原构造签名但去掉 `VulkanStagePool` 参数：`(VulkanContext const&, VmaAllocator, VulkanBufferCache&, VulkanBufferBinding, BufferUsage, uint32_t)`；成员 `mBuffer` 为 `NS_UTILS::SharedPtr<VulkanBuffer>`（构造时经 `bufferCache.Acquire(binding, numBytes)` 取得），保留 `mStagingBufferBypassEnabled`/`mAllocator`/`mBufferCache`/`mUsage`，不移植 `mStagePool`/`mLastReadAge`。SHALL 提供 `GetVkBuffer()`（`mBuffer->GetGpuBuffer()->vkbuffer`）与私有 `GetBinding()`。`loadFromCpu`、`referencedBy` 本次 SHALL 不移植。类 SHALL 位于 `src/vulkan/buffer/VulkanBufferProxy.h/.cpp`。

#### Scenario: 构造取得缓冲

- **WHEN** 以合法 binding 与字节数构造 `VulkanBufferProxy`
- **THEN** `mBuffer` 指向缓存池分配的 `VulkanBuffer`，`GetVkBuffer()` 返回其 VkBuffer 句柄

#### Scenario: binding 透传

- **WHEN** 构造后内部查询 binding
- **THEN** 返回 `mBuffer->GetGpuBuffer()->binding`，与构造入参一致

### Requirement: 适配约束

vulkan-buffer 各类型 SHALL 位于 `Backend` 命名空间（`BEGIN_NS_BACKEND`）；引用计数与持有经 `Utils::Ref` + `Utils::SharedPtr`（`resource_ptr` 零改动替代）；断言/日志映射：`assert_invariant` → `LOG_ASSERT`、`PANIC_LOG` → `LOG_CRITICAL`、`FVK_LOGD/FVK_LOGE` → `LOG_DEBUG/LOG_ERROR`、`FVK_ENABLED(FVK_DEBUG_VULKAN_BUFFER_CACHE)` → `BVK_ENABLED(BVK_DEBUG_VULKAN_BUFFER_CACHE)`、`FVK_SYSTRACE_*` 与 `utils::io::endl` 不移植；头文件 `#pragma once`，include 顺序与注释按项目规范（不保留 Filament license/文件头注释）。

#### Scenario: 编译通过

- **WHEN** 包含 `VulkanBuffer.h`/`VulkanBufferCache.h`/`VulkanBufferProxy.h` 并链接 `VulkanBufferCache.cpp`/`VulkanBufferProxy.cpp`
- **THEN** 编译通过，不依赖 Filament 任何头文件

#### Scenario: 日志宏有效

- **WHEN** 触发 `BVK_DEBUG_VULKAN_BUFFER_CACHE` 日志分支
- **THEN** 使用项目 `LOG_DEBUG`/`LOG_ERROR` 输出，编译无宏缺失

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
