# Change: port-vulkan-stage-buffer

## Why

`port-vulkan-buffer` 移植 `VulkanBufferProxy` 时砍掉了 `loadFromCpu`，其唯一依赖 `VulkanStagePool` 因此留空——CPU→GPU 的上传通道至今不存在。本次先把上传通道的**缓冲侧**落地：`VulkanStage`（改名 `VulkanStageBuffer`）与其切分单元 `Segment` 提供可持久映射的共享暂存内存，`VulkanStagePool` 负责跨帧复用与淘汰。这是后续补 `loadFromCpu` 的前置条件，同时验证 `ResourceManager::AllocateAndConstruct` 能否用于**嵌套类型**（`VulkanStageBuffer::Segment`），并把缓冲对象的独占所有权按项目智能指针约定表达（继承 `Ref`、池持 `UniquePtr`）。

`VulkanStageImage` 一并被上游放在 `VulkanStagePool` 里，但它依赖 `VulkanCommands`（命令录制层）、`VulkanLayout`/`transitionLayout`（图像布局转换）、`PixelDataFormat`/`PixelDataType`/`getVkFormat`（像素格式映射）三样本项目完全不具备的设施，本次不移植。

## What Changes

- 移植 `VulkanStage` → `VulkanStageBuffer` 到 `src/vulkan/stage/VulkanStageBuffer.h/.cpp`（含嵌套 `Segment` 资源类，按用户决策改名）
- 移植 `VulkanStagePool` 到 `src/vulkan/stage/VulkanStagePool.h/.cpp`，**砍掉全部图像路径**：`AcquireImage`、`mFreeImages`、`gc()`/`terminate()` 的图片分支、构造参数 `VulkanCommands*`
- 构造签名按本项目惯例改为 `(VulkanContext const&, ResourceManager&, VmaAllocator)`：非相干原子大小改经 `VulkanContext::GetPhysicalDeviceLimits()` 获取，资源管理改引用
- VMA 内存用法对齐本项目 `VulkanBufferCache` 的 VMA 3.x 写法（`VMA_MEMORY_USAGE_AUTO` + `VMA_ALLOCATION_CREATE_MAPPED_BIT` + `HOST_ACCESS_SEQUENTIAL_WRITE_BIT`），不再 `vmaMapMemory`/`vmaUnmapMemory`
- `resource_ptr<Segment>::construct(...)` → `ResourceManager::AllocateAndConstruct<VulkanStageBuffer::Segment>(...)`，返回 `Utils::SharedPtr`
- `VulkanStageBuffer` 改为继承 `NS_UTILS::Ref`，由 `VulkanStagePool` 以 `NS_UTILS::UniquePtr` 独占持有：`allocateNewStage` 经 `MakeUnique` 构造并返回独占引用，`destroyStage` 退化为释放该引用，`vmaDestroyBuffer` 内聚到 `~VulkanStageBuffer`（决策与备选方案见 design D10）
- 补充 `vulkan-resource` 类型表：`GetTypeEnum<VulkanStageBuffer::Segment>` 特化（**声明落在 `VulkanStageBuffer.h`**——嵌套类型无法前向声明，`Resource.h` 里那套「前向声明 + 头文件声明」模式不适用）、`DestroyWithType` 的 `StageSegment` 分支
- 不移植：`VulkanStageImage`、`AcquireImage`、`PixelDataFormat`/`PixelDataType`/`getVkFormat`、`VulkanLayout`/`transitionLayout`/`getImageAspect`、`VulkanCommands`、`FVK_SYSTRACE_*`

## Capabilities

### New Capabilities

- `vulkan-stage-buffer`: 可切分的共享 CPU-GPU 暂存缓冲（`VulkanStageBuffer` + `Segment`）与跨帧复用/淘汰的池（`VulkanStagePool`，纯 buffer 形态），基于 vulkan-resource 生命周期层

### Modified Capabilities

- `vulkan-resource`: 类型表新增第一个**嵌套类型**资源 `VulkanStageBuffer::Segment`（`STAGE_SEGMENT`），并暴露特化声明的存放约束（不能放 `Resource.h`）

## Impact

- 新增：`src/vulkan/stage/VulkanStageBuffer.h/.cpp`、`src/vulkan/stage/VulkanStagePool.h/.cpp`
- 修改：`src/vulkan/resource/ResourceManager.cpp`（include stage 头 + `DestroyWithType` 的 `StageSegment` 分支）
- 不改：`src/vulkan/resource/Resource.h`（嵌套类型无法前向声明，特化声明落在 stage 头）；CMake 经 `file(GLOB_RECURSE)` 自动收集，无需改动
- 不引入新依赖：VMA 已由 `port-vulkan-buffer` 接入（`3rd/vulkan*` + `src/vulkan/VmaImpl.cpp`）；`NS_UTILS::UniquePtr`/`MakeUnique` 取自既有 `3rd/Utils` 子模块
- 运行期无调用方（预期内）：`AcquireStage` 等 `loadFromCpu` 接入，`Gc()` 等 driver 帧循环接入；本次验证手段为全量构建 + tests 无回归
