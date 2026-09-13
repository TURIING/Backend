# Change: port-vulkan-index-buffer

## Why

VulkanDriver 的缓冲族目前只有顶点缓冲与缓冲对象两条路径，索引缓冲（`createIndexBuffer` / `destroyIndexBuffer`）整条链路缺失。索引缓冲是第一个"非顶点"缓冲资源：它与 `VulkanBufferObject` 共用 `VulkanBufferProxy` + `VulkanBufferCache`，却有自己的 `HwIndexBuffer` 位域形态与 `VkIndexType` 推导，是补齐"客户端句柄 → 资源对象 → GPU 内存 → GC 归还池块"闭环的最小完整样本。

同时暴露了一个必须一起收口的缺陷：上一变更（port-vulkan-buffer）把 `VulkanVertexBuffer` / `VulkanBufferObject` 接进 driver 时，`vulkan-resource` 类型表只补到 `VulkanVertexBufferInfo`——`VulkanBufferObject` 缺 `GetTypeEnum` 特化（回退 `UndefinedType`）、`VulkanVertexBuffer` 缺 `DestroyWithType` 分支，两者销毁时句柄池块永不归还。更关键的是 `ResourceManager::Make` 构造后无人持有引用：返回的 `SharedPtr` 在 driver 方法末尾析构即计数归零，对象**在创建时就被排入 GC 队列**，而"销毁才入队"才是闭环语义。今天不炸只是因为 `ResourceManager::Gc()` 仅在 `Terminate()` 中被调用、帧循环尚未泵 GC。

## What Changes

- 新增 `HwIndexBuffer`（`src/HwDefine.h`）：`count:26 / elementSize:5 / asynchronous:1` 位域 + 构造约束
- 新增 `Driver::GetElementTypeSize(ElementType)` 公共静态成员：`include/Backend/Driver.h` 声明、独立编译单元 `src/Driver.cpp` 定义（上游 `getElementTypeSize` 的小写拼写按项目规范改为 PascalCase）
- `DriverAPI.inc` 追加缓冲族 2 条：`CreateIndexBuffer`（TAGGED_R_N）、`DestroyIndexBuffer`；`CommandStream` 经既有宏自动生成对应记录方法，无需手写
- 新增 `VulkanIndexBuffer`（`src/vulkan/VulkanHandle.h/.cpp`）：继承 `HwIndexBuffer` + `Resource`，公开 `VkIndexType const indexType`，底层复用 `VulkanBufferProxy`（binding=Index、usage=STATIC、字节数 `elementSize * indexCount`）
- `VulkanDriver` 实现 `CreateIndexBufferS` / `CreateIndexBufferR` / `DestroyIndexBuffer`
- **修补 `vulkan-resource` 句柄引用语义（BREAKING 语义修正）**：`Make` 构造后为句柄额外持有一份引用；`Destroy` 释放该引用；`AllocateAndConstruct` 改为不经 `Make`、返回拥有所有权的 `SharedPtr`（内部资源不受影响）
- 补齐 `vulkan-resource` 类型表：`GetTypeEnum<VulkanBufferObject>` / `GetTypeEnum<VulkanIndexBuffer>` 特化，`DestroyWithType` 增加 `BufferObject` / `IndexBuffer` / `VertexBuffer` 三个销毁分支
- `tests/` 增加索引缓冲创建-销毁往返，端到端验证命令流 → 驱动 → 资源层 → 终止期 GC 链路
- **修复两处阻塞上述验证的既有缺陷（实施中新增，用户决策）**：`VulkanDriver::Create` 补 `handleArenaSize` 兜底（上游默认 8MB，本项目 0 字节 arena 会在驱动构造期越界断言）；`DestroyResources` 补池 `Terminate()`（否则池内 VkBuffer 未归还 VMA）并改为可重入（`terminate()` 与析构都会调用它）

**不移植**：`updateIndexBuffer` / `updateIndexBufferAsync`、`BufferDescriptor`、`Driver::scheduleDestroy`、`VulkanBufferProxy::LoadFromCpu`、`VulkanCommands` 接线、`VulkanRenderPrimitive`、`vkCmdBindIndexBuffer` 绘制路径。

## Capabilities

### New Capabilities

（无：本次不引入新能力域，全部为既有能力域扩展）

### Modified Capabilities

- `vulkan-buffer`: 新增索引缓冲资源形态（`HwIndexBuffer`、`VulkanIndexBuffer`）与驱动接线
- `vulkan-resource`: 类型表在缓冲族收口（2 个特化 + 3 个销毁分支）；确立"句柄持有一份引用"的构造/销毁语义
- `driver-interface`: 驱动方法清单追加缓冲族 2 条并明确命名分组；新增 `Driver::GetElementTypeSize`

## Impact

- 新增：`src/Driver.cpp`
- 修改：
  - `include/Backend/Driver.h`（+1 静态方法声明）
  - `include/Backend/DriverAPI.inc`（+2 条缓冲族方法）
  - `src/HwDefine.h`（+`HwIndexBuffer`）
  - `src/vulkan/VulkanHandle.h` / `VulkanHandle.cpp`（+`VulkanIndexBuffer`）
  - `src/vulkan/VulkanDriver.cpp`（+3 个方法）
  - `src/vulkan/resource/Resource.h` / `Resource.cpp`（+2 特化）
  - `src/vulkan/resource/ResourceManager.h`（`Make` / `AllocateAndConstruct` / `Destroy` 语义）
  - `src/vulkan/resource/ResourceManager.cpp`（+3 销毁分支）
  - `tests/Engine.h` / `Engine.cpp` / `App.cpp`（索引缓冲往返）
- CMake：`src/Driver.cpp` 由 `file(GLOB_RECURSE src/*.cpp)` 自动收集，构建脚本无需改动
- 不改：`VulkanBufferProxy`、`VulkanBufferCache`（索引缓冲直接复用现有代理与缓存池形态）
