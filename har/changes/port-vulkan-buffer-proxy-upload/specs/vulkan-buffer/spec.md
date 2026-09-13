## MODIFIED Requirements

### Requirement: BufferUsage 枚举

`BufferUsage` 枚举 SHALL 位于 `include/Backend/DriverDefine.h`（driver 通用定义文件，供多 backend 复用）：`STATIC=0`、`DYNAMIC=1`、`DYNAMIC_BIT=0x1`、`SHARED_WRITE_BIT=0x04`。SHALL 提供 `constexpr operator|` 与 `constexpr operator&`（按 `uint8_t` 基底转换），以及 `constexpr bool HasAnyFlag(BufferUsage value, BufferUsage flags)`（任一标志命中即真）。因 `STATIC` 恰为 0，SHALL NOT 提供 `operator bool`。SHALL NOT 移植上游 `utils::EnableBitMaskOperators` 模板设施（本地 Utils 无此设施）。

#### Scenario: 枚举值一致

- **WHEN** 引用 `BufferUsage::STATIC` / `DYNAMIC_BIT` / `SHARED_WRITE_BIT`
- **THEN** 数值与原 Filament `backend/DriverEnums.h` 一致

#### Scenario: 组合与命中判定

- **WHEN** 求 `BufferUsage::STATIC | BufferUsage::SHARED_WRITE_BIT` 并对其调用 `HasAnyFlag(..., BufferUsage::SHARED_WRITE_BIT)`
- **THEN** 组合值为 0x04 且判定为真

#### Scenario: 未命中

- **WHEN** 对 `BufferUsage::DYNAMIC` 调用 `HasAnyFlag(..., BufferUsage::SHARED_WRITE_BIT)`
- **THEN** 判定为假

### Requirement: VulkanContext 适配访问器

`VulkanContext` SHALL 提供 `IsStagingBufferBypassEnabled()` 只读访问器返回 `m_stagingBufferBypassEnabled`（成员由 VulkanPlatform 写入），供 `VulkanBufferProxy` 判定直写路径。SHALL 亦有 `IsUnifiedMemoryArchitecture()` 返回 `m_isUnifiedMemoryArchitecture`。

#### Scenario: 读取开关

- **WHEN** 调用 `context->IsStagingBufferBypassEnabled()`
- **THEN** 返回 `m_stagingBufferBypassEnabled` 当前值

### Requirement: VulkanBufferProxy 动态代理

`VulkanBufferProxy` SHALL 提供构造 `(const VulkanContextPtr&, VmaAllocator, const VulkanStagePoolPtr&, const VulkanBufferCachePtr&, VulkanBufferBinding, BufferUsage, uint32_t)`；成员 `m_buffer` 为 `NS_UTILS::SharedPtr<VulkanBuffer>`（构造时经 `bufferCache->Acquire(binding, numBytes)` 取得），成员 SHALL 含 `m_stagingBufferBypassEnabled` / `m_allocator` / `m_stagePool` / `m_bufferCache` / `m_usage` / `m_lastReadAge`（初值 0）。SHALL 提供 `GetVkBuffer()`（`m_buffer->GetGpuBuffer()->vkbuffer`）、`ReferencedBy(commands)` 与私有 `GetBinding()`。

`LoadFromCpu(commands, cpuData, byteOffset, numBytes)` SHALL 以 `m_buffer->GetRefCount() == 1` 判定缓冲空闲，随后 `commands.Acquire(m_buffer)` 登记借用。满足「(空闲且 staging 直通开启) 或 usage 含 STATIC/SHARED_WRITE」且映射内存非空时 SHALL 走直写路径（`memcpy` 到 `allocationInfo.pMappedData + byteOffset`，`vmaFlushAllocation` 后返回）；否则 SHALL 经 `m_stagePool->AcquireStage(numBytes)` 取暂存段、`commands.Acquire(stage)` 登记借用、`memcpy` 入段映射内存并 `vmaFlushAllocation`，再录制 `vkCmdCopyBuffer`（src 为段缓冲与段偏移，dst 为目标缓冲与 `byteOffset`）。

staging 路径 SHALL 在发生同命令缓冲内读后写（`commands.Age() == m_lastReadAge`）时于拷贝前插入 `VkBufferMemoryBarrier`（src 按 binding 取读访问与读阶段，dst 为 `VK_ACCESS_TRANSFER_WRITE_BIT` + `VK_PIPELINE_STAGE_TRANSFER_BIT`），并在拷贝后 SHALL 插入后置屏障（src 为 `VK_ACCESS_TRANSFER_WRITE_BIT` + `VK_PIPELINE_STAGE_TRANSFER_BIT`，dst 按 binding 映射：Index → `VK_ACCESS_INDEX_READ_BIT` + `VK_PIPELINE_STAGE_VERTEX_INPUT_BIT`，Vertex → `VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT` + 同阶段，Uniform → `VK_ACCESS_UNIFORM_READ_BIT` + 顶点/片元着色器阶段，ShaderStorage 不追加）。

`ReferencedBy(commands)` SHALL 调用 `commands.Acquire(m_buffer)` 并将 `commands.Age()` 记入 `m_lastReadAge`。

类 SHALL 位于 `src/vulkan/buffer/VulkanBufferProxy.h/.cpp`。

#### Scenario: 构造取得缓冲

- **WHEN** 以合法 binding 与字节数构造 `VulkanBufferProxy`
- **THEN** `m_buffer` 指向缓存池分配的 `VulkanBuffer`，`GetVkBuffer()` 返回其 VkBuffer 句柄

#### Scenario: binding 透传

- **WHEN** 构造后内部查询 binding
- **THEN** 返回 `m_buffer->GetGpuBuffer()->binding`，与构造入参一致

#### Scenario: 直写路径

- **WHEN** 缓冲空闲、映射内存非空且满足直通或 STATIC/SHARED_WRITE 条件时调用 `LoadFromCpu`
- **THEN** 数据经 `memcpy` 写入映射内存并 `vmaFlushAllocation`，不取暂存段、不录制 `vkCmdCopyBuffer`

#### Scenario: staging 路径

- **WHEN** 不满足直写条件时调用 `LoadFromCpu`
- **THEN** 取得暂存段并 `memcpy` 入段、`vmaFlushAllocation`，随后录制 `vkCmdCopyBuffer` 与拷贝后的 `VkBufferMemoryBarrier`

#### Scenario: 读后写屏障

- **WHEN** 同一命令缓冲已 `ReferencedBy` 后再调用 `LoadFromCpu`（`Age()` 未变）
- **THEN** 在 `vkCmdCopyBuffer` 之前额外插入 src 为对应读访问、dst 为 `VK_ACCESS_TRANSFER_WRITE_BIT` 的屏障

#### Scenario: 借用登记

- **WHEN** `LoadFromCpu` 或 `ReferencedBy` 执行
- **THEN** 目标 `VulkanBuffer` 被登记到命令缓冲的借用列表，提交完成前不会被 GC 回收

### Requirement: VulkanIndexBuffer 资源对象

`VulkanIndexBuffer` SHALL 定义于 `src/vulkan/VulkanHandle.h/.cpp`，public 继承 `HwIndexBuffer` 与 `Resource`，构造签名 SHALL 为 `(const VulkanContextPtr&, VmaAllocator, const VulkanStagePoolPtr&, const VulkanBufferCachePtr&, uint8_t elementSize, uint32_t indexCount)`（与本地 `VulkanBufferProxy` 形态一致）。SHALL 组合私有成员 `VulkanBufferProxy m_buffer`，以 `VulkanBufferBinding::Index`、`BufferUsage::STATIC`、字节数 `elementSize * indexCount` 构造，并提供 `NODISCARD VkBuffer GetVkBuffer() const noexcept` 透传底层句柄。SHALL 提供公开常量成员 `VkIndexType const indexType`：`elementSize == sizeof(uint16_t)` 时为 `VK_INDEX_TYPE_UINT16`，否则为 `VK_INDEX_TYPE_UINT32`。SHALL 以 `DECLARE_SHARE_PTR_CLASS` 声明 `VulkanIndexBufferPtr`。

#### Scenario: 32 位索引推导

- **WHEN** 以 `elementSize = 4`、`indexCount = n` 构造
- **THEN** `indexType` 为 `VK_INDEX_TYPE_UINT32`，底层 `VulkanBufferProxy` 的 binding 为 `Index`，`GetVkBuffer()` 返回非空 `VkBuffer`

#### Scenario: 16 位索引推导

- **WHEN** 以 `elementSize = 2` 构造
- **THEN** `indexType` 为 `VK_INDEX_TYPE_UINT16`

#### Scenario: 基类字段继承

- **WHEN** 构造后读取基类字段
- **THEN** `count` 等于构造入参 `indexCount`，`asynchronous` 为 false

#### Scenario: 暂存池透传

- **WHEN** 驱动以 `m_stagePool` 构造 `VulkanIndexBuffer`
- **THEN** 代理持有该暂存池，`LoadFromCpu` 可经其取得暂存段
