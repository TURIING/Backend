# Change: port-vulkan-buffer-proxy-upload

## Why

`port-vulkan-buffer` 当时按用户决策把 `VulkanBufferProxy` 砍成只读代理：不移植 `loadFromCpu` / `referencedBy`，构造签名去掉 `VulkanStagePool` 参数。于是 `src/vulkan/VulkanHandle.cpp` 里 `VulkanBufferObject::LoadFromCpu` 至今是空桩（注释：上传通道尚未移植，内容不会写入 GPU），索引缓冲同理——CPU 侧数据没有任何进入 GPU 的路径。

本次补齐**代理层**的上传能力：CPU 数据经 staging buffer（UMA / STATIC 场景直写映射内存）落到 `VkBuffer`，携带必要的转移屏障，并在命令缓冲上登记借用。

范围限定在代理层（用户决策）：`BufferDescriptor`、`Driver::scheduleDestroy`、驱动 `updateBufferObject` / `updateIndexBuffer` 接口与资源层转发留待后续变更，故本次产出的两个方法暂无调用方，属"先把通道备好"。

## What Changes

- `VulkanBufferProxy` 回补 `VulkanStagePool`：构造签名加 `const VulkanStagePoolPtr&`，成员补 `m_stagePool` 与 `m_lastReadAge`
- 移植 `LoadFromCpu(commands, cpuData, byteOffset, numBytes)`：可用性判定 → 直写路径（`memcpy` + `vmaFlushAllocation`，条件为 UMA 直通且缓冲空闲，或 usage 含 STATIC/SHARED_WRITE）→ staging 路径（`AcquireStage` + `vkCmdCopyBuffer` + 读写两处 `VkBufferMemoryBarrier`），屏障的访问掩码与阶段按 binding 选择
- 移植 `ReferencedBy(commands)`：登记缓冲被命令缓冲借用并记录 `Age()`，供 `LoadFromCpu` 判定是否需要读后写屏障
- `BufferUsage` 补位运算：`constexpr operator|` / `operator&`，并把上游 `any(mUsage & (...))` 收敛为具名 `HasAnyFlag(value, flags)`
- 构造签名连锁：`VulkanBufferObject` / `VulkanIndexBuffer` 构造增加 `stagePool` 参数，`VulkanDriver::CreateBufferObjectR` / `CreateIndexBufferR` 传入 `m_stagePool`

**不移植**（留待后续变更）：`BufferDescriptor` 类型、`Driver::scheduleDestroy`、驱动 `updateBufferObject` / `updateIndexBuffer`（含异步变体与 `CallbackHandler`）、`VulkanBufferObject::LoadFromCpu` 空桩转正、`VulkanIndexBuffer::LoadFromCpu` 暴露。

## Capabilities

### New Capabilities

（无：本次不引入新能力域）

### Modified Capabilities

- `vulkan-buffer`: `VulkanBufferProxy` 从只读代理升级为可写代理（回补 stagePool / `LoadFromCpu` / `ReferencedBy`）；`BufferUsage` 增加位运算；`VulkanIndexBuffer` 构造签名随代理变化

## Impact

- 修改：
  - `src/vulkan/buffer/VulkanBufferProxy.h` / `VulkanBufferProxy.cpp`（主体）
  - `include/Backend/DriverDefine.h`（`BufferUsage` 位运算 + `HasAnyFlag`）
  - `src/vulkan/VulkanHandle.h` / `VulkanHandle.cpp`（`VulkanBufferObject` / `VulkanIndexBuffer` 构造签名）
  - `src/vulkan/VulkanDriver.cpp`（两个创建点传入 `m_stagePool`）
- 不改：`VulkanStagePool`（`AcquireStage` 已具备）、`VulkanBufferCache`、`DriverAPI.inc`、CMake
- 验证：构建 + 运行 `BackendTests`（索引缓冲/缓冲对象创建路径覆盖新构造签名）；`LoadFromCpu` / `ReferencedBy` 无调用方，本次仅以编译与语义对照验证
