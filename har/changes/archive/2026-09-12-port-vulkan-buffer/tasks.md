# Tasks: port-vulkan-buffer

## 1. VMA 依赖接入

- [x] 1.1 `git submodule add` VulkanMemoryAllocator（固定 commit）到 `3rd/VulkanMemoryAllocator`
- [x] 1.2 创建 `3rd/vma.cmake`：INTERFACE target `vma`（include `3rd/VulkanMemoryAllocator/include`），接入 `3rd/CMakeLists.txt` 并追加 `THIRD_PARTY_LIBS`；Backend target 链接 vma（PRIVATE include 路径）
- [x] 1.3 创建 `src/vulkan/VmaImpl.cpp`：`VMA_STATIC_VULKAN_FUNCTIONS=0` + `VMA_DYNAMIC_VULKAN_FUNCTIONS=1` + `VMA_IMPLEMENTATION`，include `vk_mem_alloc.h`
- [x] 1.4 增量构建验证 VMA 符号（`vmaCreateBuffer` 等）可编译链接

## 2. 定义层移植

- [x] 2.1 `include/Backend/DriverDefine.h` 增加 `BufferUsage` 枚举（STATIC/DYNAMIC/DYNAMIC_BIT/SHARED_WRITE_BIT，置于 `DriverConfig` 之前）
- [x] 2.2 `src/vulkan/VulkanContext.h` 增加 `stagingBufferBypassEnabled()` 访问器（读 `m_stagingBufferBypassEnabled`）
- [x] 2.3 创建 `src/vulkan/buffer/VulkanBuffer.h`：`VulkanBufferBinding` 枚举 + `VulkanGpuBuffer` 结构 + `VulkanBuffer` 类（继承 `Backend::Resource`，`OnRecycle` 回调，`GetGpuBuffer()`）

## 3. 移植 VulkanBufferCache

- [x] 3.1 创建 `src/vulkan/buffer/VulkanBufferCache.h`：构造 `(VulkanContext const&, ResourceManager&, VmaAllocator)`、`Acquire`/`Gc`/`Terminate`、私有 `Release`/`Allocate`/`Destroy`/`GetPool`、4 池 + `mCurrentFrame`
- [x] 3.2 创建 `src/vulkan/buffer/VulkanBufferCache.cpp`：`getVkBufferUsage` 映射、`Acquire`（lower_bound 复用 + `AllocateAndConstruct<VulkanBuffer>` + OnRecycle 绑 `Release`）、`Gc`（3 帧保护 + LRU 淘汰 + `BVK_DEBUG_VULKAN_BUFFER_CACHE` 日志）、`Terminate`、`Allocate`（UMA 分支 + `vmaCreateBuffer`）、`Destroy`（`vmaDestroyBuffer` + delete）、`GetPool`（UNKNOWN → `LOG_CRITICAL`）

## 4. 移植 VulkanBufferProxy

- [x] 4.1 创建 `src/vulkan/buffer/VulkanBufferProxy.h`：构造签名 `(VulkanContext const&, VmaAllocator, VulkanBufferCache&, VulkanBufferBinding, BufferUsage, uint32_t)`（无 stagePool）、`GetVkBuffer()`、私有 `GetBinding()`；成员 `SharedPtr<VulkanBuffer> mBuffer` + 保留 `mStagingBufferBypassEnabled`/`mAllocator`/`mBufferCache`/`mUsage`（不移植 `mStagePool`/`mLastReadAge`/`loadFromCpu`/`referencedBy`）
- [x] 4.2 创建 `src/vulkan/buffer/VulkanBufferProxy.cpp`：构造（`mBuffer = bufferCache.Acquire(binding, numBytes)`）、`GetVkBuffer`、`GetBinding`

## 5. 资源层类型表补充

- [x] 5.1 `src/vulkan/resource/Resource.h`：前向声明 `class VulkanBuffer`；`GetTypeEnum<VulkanBuffer>` 特化声明（类外）；`Resource.cpp` 定义特化返回 `ResourceType::VulkanBuffer`
- [x] 5.2 `src/vulkan/resource/ResourceManager.h`：`construct` 中 `GetTypeEnum<D>()` 改为 `obj->GetTypeEnum<D>()`；新增私有 `destruct<D, B>(Handle<B>)`（HandleCast + `Deallocate`）
- [x] 5.3 `src/vulkan/resource/ResourceManager.cpp`：include `vulkan/buffer/VulkanBuffer.h`；`DestroyWithType` 增加 `VulkanBuffer` 分支（`destruct<VulkanBuffer>(Handle<VulkanBuffer>(id))`）

## 6. 构建验证

- [x] 6.1 全量构建（`cmake --build build`）：新 cpp 经 GLOB_RECURSE 自动收集，VMA 链接通过，无编译错误
- [x] 6.2 构建 `tests`（`BackendTests`）确认既有功能无回归
- [x] 6.3 复核：`GetTypeEnum<VulkanBuffer>` 特化被正确实例化（construct 模板无编译错误）、`DestroyWithType` VulkanBuffer 分支可达
