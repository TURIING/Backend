## MODIFIED Requirements

### Requirement: 种子方法清单

`DriverAPI.inc` 末尾 SHALL 追加种子方法清单，覆盖三条宏路径：`DECL_DRIVER_API_0(tick)`、`DECL_DRIVER_API_N(beginFrame, int64_t, monotonic_clock_ns, int64_t, refreshIntervalNs, uint32_t, frameId)`、`DECL_DRIVER_API_0(flush)`、`DECL_DRIVER_API_0(finish)`、`DECL_DRIVER_API_0(resetState)`、`DECL_DRIVER_API_TAGGED_R_N(FenceHandle, createFence)`、`DECL_DRIVER_API_N(destroyFence, FenceHandle, fh)`、`DECL_DRIVER_API_SYNCHRONOUS_0(void, terminate)`。方法名 SHALL 保持 Filament 原版拼写（与 `Driver` 声明严格一致，是 `COMMAND_TYPE` 机制的前提）；该拼写约束 SHALL 仅适用于本种子集，非种子方法（如缓冲族）的命名见「缓冲族方法清单」。类型名 SHALL 不加 `backend::` 前缀（本地命名空间为 `Backend`，inc 在命名空间内展开）。

#### Scenario: 覆盖三条宏路径

- **WHEN** 统计种子集
- **THEN** `tick`/`flush`/`finish`/`resetState` 覆盖 0 参异步路径，`beginFrame` 覆盖多参异步路径，`createFence` 覆盖 RETURN 路径，`terminate` 覆盖同步路径

### Requirement: VulkanDriver 适配

`VulkanDriver` SHALL 实现 `Dispatcher GetDispatcher() const noexcept override`（返回 `ConcreteDispatcher<VulkanDriver>::Make()`）与 `void terminate() override`（转发到 `DestroyResources()`）。`Create(VulkanPlatform*, const VulkanContextPtr&, const DriverConfig&)` SHALL 保持签名不变，并在构造驱动前把 `DriverConfig::handleArenaSize` 兜底到不小于 8MB（上游 `FVK_HANDLE_ARENA_SIZE_IN_MB` 默认值）：`0`（未配置）会得到空 arena 并在 `HandleAllocator` 初始化时越界断言。

`DestroyResources()` SHALL 按序完成：`ResourceManager::Terminate()`（清空 GC 队列，资源引用归零并把池内缓冲交还缓存）→ 各池 `Terminate()`（归还 VMA）→ 各池 `Reset()` → `vmaDestroyAllocator`。该方法 SHALL 幂等——`terminate()` 与 `~VulkanDriver()` 都会调用它，每步须判空后成对执行。

#### Scenario: VulkanDriver 可编译

- **WHEN** 构建整个 `Backend` 静态库
- **THEN** `src/vulkan/VulkanDriver.cpp` 编译通过，`Create()` 返回 `DriverPtr`

#### Scenario: 默认配置兜底 arena

- **WHEN** 以 `DriverConfig{}`（`handleArenaSize = 0`）调用 `Create()`
- **THEN** 驱动以不小于 8MB 的句柄 arena 构造，不触发 `HandleAllocator` 的越界断言

#### Scenario: 终止期归还顺序

- **WHEN** `terminate()` 执行完毕
- **THEN** GC 队列为空、各池内 VkBuffer 已 `vmaDestroyBuffer` 归还 VMA、`VmaAllocator` 已销毁；`vmaDestroyAllocator` 不因残留分配断言

#### Scenario: 重复终止

- **WHEN** `terminate()` 之后再执行析构（`DestroyResources` 第二次进入）
- **THEN** 不解引用已释放对象，进程正常退出

## ADDED Requirements

### Requirement: 缓冲族方法清单

`DriverAPI.inc` SHALL 维护缓冲族方法清单，与种子集分块相邻放置（创建类在前、销毁类在后、绑定类居末），当前 SHALL 包含：

- 创建（`DECL_DRIVER_API_TAGGED_R_N`）：`CreateVertexBufferInfo`、`CreateVertexBuffer`、`CreateBufferObject`、`CreateIndexBuffer`
- 销毁（`DECL_DRIVER_API_N`）：`DestroyBufferObject`、`DestroyIndexBuffer`
- 绑定（`DECL_DRIVER_API_N`）：`SetVertexBufferObject`

缓冲族方法名 SHALL 采用项目 `PascalCase` 规范（不沿用 Filament 的 `createXxx` 小写拼写），组内保持一致。`CreateIndexBuffer` SHALL 声明为 `DECL_DRIVER_API_TAGGED_R_N(IndexBufferHandle, CreateIndexBuffer, ElementType, elementType, uint32_t, indexCount, BufferUsage, usage)`，`DestroyIndexBuffer` SHALL 声明为 `DECL_DRIVER_API_N(DestroyIndexBuffer, IndexBufferHandle, ibh)`。

#### Scenario: 索引缓冲方法成对展开

- **WHEN** `Driver` 展开 `DriverAPI.inc`
- **THEN** 得到纯虚 `IndexBufferHandle CreateIndexBufferS() noexcept` 与 `void CreateIndexBufferR(IndexBufferHandle, ElementType, uint32_t, BufferUsage, NS_UTILS::ImmutableString&&)`，以及非虚 `void DestroyIndexBuffer(IndexBufferHandle)`

#### Scenario: CommandStream 自动生成记录方法

- **WHEN** 编译包含 `CommandStream.h` 的翻译单元
- **THEN** `CommandStream::CreateIndexBuffer(ElementType, uint32_t, BufferUsage)`（返回 `IndexBufferHandle`，内部先调 `CreateIndexBufferS` 再记录 `CreateIndexBufferR`）与 `CommandStream::DestroyIndexBuffer(IndexBufferHandle)` 自动可用，无需手写方法

#### Scenario: 派发表登记

- **WHEN** `ConcreteDispatcher<VulkanDriver>::Make()` 展开清单
- **THEN** 索引缓冲两条方法均登记进 `Dispatcher`，`CreateIndexBuffer` 绑定 `CreateIndexBufferR`

### Requirement: 元素类型字节数换算

`Driver` SHALL 提供公共静态成员 `NODISCARD static size_t GetElementTypeSize(ElementType type) noexcept`：`include/Backend/Driver.h` 声明、`src/Driver.cpp` 定义。SHALL 覆盖 `ElementType` 全部 26 个取值并返回其字节数（`BYTE`→1、`BYTE2`→2、`BYTE3`→3、`BYTE4`→4，`UBYTE`/`SHORT`/`USHORT`/`INT`/`UINT`/`FLOAT`/`HALF` 各按分量宽度同理展开）。方法名 SHALL 采用项目 `PascalCase` 规范（上游为 `getElementTypeSize`）；SHALL 不抛异常、对未覆盖取值返回 0。

#### Scenario: 标量与向量

- **WHEN** 调用 `GetElementTypeSize(ElementType::BYTE)` / `(ElementType::UBYTE3)` / `(ElementType::FLOAT4)`
- **THEN** 分别返回 1 / 3 / 16

#### Scenario: 全枚举覆盖

- **WHEN** 遍历 `ElementType` 全部 26 个取值调用
- **THEN** 无一个返回 0

#### Scenario: 可经具体驱动无对象调用

- **WHEN** 在 `VulkanDriver::CreateIndexBufferR` 中调用 `Driver::GetElementTypeSize(elementType)`
- **THEN** 编译通过，无需驱动实例
