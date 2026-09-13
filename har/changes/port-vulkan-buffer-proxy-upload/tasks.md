# Tasks: port-vulkan-buffer-proxy-upload

## 1. BufferUsage 位运算

- [x] 1.1 `include/Backend/DriverDefine.h`：在 `BufferUsage` 枚举之后追加 `constexpr BufferUsage operator|(BufferUsage, BufferUsage) noexcept`、`constexpr BufferUsage operator&(BufferUsage, BufferUsage) noexcept`（按 `uint8_t` 基底转换）与 `constexpr bool HasAnyFlag(BufferUsage value, BufferUsage flags) noexcept`；不提供 `operator bool`（`STATIC` 为 0）

## 2. VulkanBufferProxy 回补

- [x] 2.1 `src/vulkan/buffer/VulkanBufferProxy.h`：include `vulkan/stage/VulkanStagePool.h`，前置声明 `struct VulkanCommandBuffer;`；构造签名改为 `(const VulkanContextPtr&, VmaAllocator, const VulkanStagePoolPtr&, const VulkanBufferCachePtr&, VulkanBufferBinding, BufferUsage, uint32_t)`；成员补 `m_stagePool`（`VulkanStagePoolPtr`）与 `m_lastReadAge`（`uint32_t`，初值 0）；声明 `void LoadFromCpu(VulkanCommandBuffer&, void const*, uint32_t, uint32_t)` 与 `void ReferencedBy(VulkanCommandBuffer&)`
- [x] 2.2 `src/vulkan/buffer/VulkanBufferProxy.cpp`：构造初始化列表补 `m_stagePool(stagePool)`；实现 `LoadFromCpu`——空闲判定（`GetRefCount() == 1`）→ `commands.Acquire(m_buffer)` → 直写路径（`memcpy` + `vmaFlushAllocation`）或 staging 路径（`AcquireStage` + `Acquire` + `memcpy` + flush + 读后写屏障 + `vkCmdCopyBuffer` + 写后屏障）
- [x] 2.3 `src/vulkan/buffer/VulkanBufferProxy.cpp`：实现 `ReferencedBy`（`commands.Acquire(m_buffer)` + `m_lastReadAge = commands.Age()`）
- [x] 2.4 补 include：`<cstring>`（memcpy）、`vulkan/VkDef.h`、`vulkan/commands/VulkanCommandBuffer.h`（完整定义）、`vulkan/stage/VulkanStageBuffer.h`

## 3. 构造签名连锁

- [x] 3.1 `src/vulkan/VulkanHandle.h` / `VulkanHandle.cpp`：`VulkanBufferObject` 构造签名在 `allocator` 与 `bufferCache` 之间插入 `const VulkanStagePoolPtr& stagePool`，并透传给 `m_buffer`
- [x] 3.2 `src/vulkan/VulkanHandle.h` / `VulkanHandle.cpp`：`VulkanIndexBuffer` 构造签名同样插入 `stagePool` 并透传
- [x] 3.3 `src/vulkan/VulkanDriver.cpp`：`CreateBufferObjectR` 与 `CreateIndexBufferR` 的 `Make<...>` 调用传入 `m_stagePool`（位置与签名一致）

## 4. 验证

- [x] 4.1 全量构建（`cmake --build build`）无错误
- [x] 4.2 运行 `bin/BackendTests`（需 `DYLD_LIBRARY_PATH=/opt/homebrew/lib` 与 `VK_ICD_FILENAMES=<VulkanSDK>/macOS/share/vulkan/icd.d/MoltenVK_icd.json`）：索引缓冲 8 帧往返正常、无 `LOG_CRITICAL`、exit 0
- [x] 4.3 与上游逐行对照复核 `LoadFromCpu`：直写判定的四个条件、屏障的 binding 分支映射、`vkCmdCopyBuffer` 的 src/dst 偏移、`Age()` 读后写判定，四处不得偏离
- [x] 4.4 复核本次范围：`VulkanBufferObject::LoadFromCpu` 仍为空桩、`DriverAPI.inc` 未新增 `update*` 方法、未引入 `BufferDescriptor`
