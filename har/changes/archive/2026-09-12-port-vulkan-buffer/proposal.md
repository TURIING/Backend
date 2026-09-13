# Change: port-vulkan-buffer

## Why

VulkanDriver 目前是空壳（44 行 cpp），需要第一个真实 GPU 资源来打通"句柄 → 资源对象 → GPU 内存"链路。VulkanBuffer/VulkanBufferCache/VulkanBufferProxy 是 Filament vulkan backend 的 buffer 管理核心：池化 LRU 缓存复用 VkBuffer、代理对象解耦外部引用。既有 `vulkan-resource` 能力域（Resource/ResourceManager，tasks 1-4 已完成）已就绪但类型表为空——VulkanBuffer 正是第一个接入真实类型、验证整条"引用归零 → GC → 归还池块"销毁链的载体。

## What Changes

- 移植 `VulkanBuffer`、`VulkanBufferCache`、`VulkanBufferProxy` 到 `src/vulkan/buffer/`（Proxy 不移植 `loadFromCpu`、`referencedBy`、`VulkanStagePool`，保留原构造签名去掉 stagePool 参数）
- 引入 VMA 依赖：`git submodule` 添加 `VulkanMemoryAllocator` 到 `3rd/`，CMake INTERFACE target + 独立 `VMA_IMPLEMENTATION` 编译单元（配合 volk 动态加载）
- 移植 `VulkanBufferBinding` 枚举与 `VulkanGpuBuffer` 结构（VMA 分配元数据），放 `src/vulkan/buffer/VulkanBuffer.h`
- 移植 `BufferUsage` 枚举到 `include/Backend/DriverDefine.h`（driver 通用定义文件，仅作 Proxy 参数与成员存储）
- `VulkanContext` 补充 `stagingBufferBypassEnabled()` 访问器（保留 Proxy 原签名需要）
- 补充 `vulkan-resource` 类型表：`GetTypeEnum<VulkanBuffer>` 特化、`ResourceManager::DestroyWithType` 的 VulkanBuffer 分支、`destruct` 模板；修复 `construct` 中 `GetTypeEnum<D>()` 无对象调用（实例化后才暴露的编译问题）
- 引用计数与持有：`Utils::SharedPtr` + `Resource`（继承 `Utils::Ref`），`resource_ptr` → `SharedPtr` 零改动替代

## Capabilities

### New Capabilities

- `vulkan-buffer`: VkBuffer 的缓存池（LRU 复用）、GPU buffer 资源对象、buffer 动态代理封装，基于 vulkan-resource 生命周期层

### Modified Capabilities

- `vulkan-resource`: 类型表从最小骨架（仅 UNDEFINED_TYPE 空分支）扩展出第一个真实类型 VulkanBuffer 的完整销毁分支；construct 模板编译修复

## Impact

- 新增：`src/vulkan/buffer/VulkanBuffer.h`、`VulkanBufferCache.h/.cpp`、`VulkanBufferProxy.h/.cpp`；`src/vulkan/VmaImpl.cpp`；`3rd/vma.cmake`；submodule `3rd/VulkanMemoryAllocator`
- 修改：`include/Backend/DriverDefine.h`（+`BufferUsage` 枚举）；`src/vulkan/VulkanContext.h`（+1 getter）；`src/vulkan/resource/Resource.h/.cpp`（GetTypeEnum 特化）；`ResourceManager.h/.cpp`（destruct 模板、DestroyWithType 分支、construct 修复）；`3rd/CMakeLists.txt`（VMA target 接入）
- CMake 源列表经 `file(GLOB_RECURSE)` 自动收集，无需改动；VMA include 路径需追加到 Backend target
- 不移植：`VulkanStagePool`、`loadFromCpu`、`referencedBy`、`resource_ptr`、`VulkanHandles.h`
