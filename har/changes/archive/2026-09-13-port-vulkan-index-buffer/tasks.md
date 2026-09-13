# Tasks: port-vulkan-index-buffer

## 1. 定义层

- [x] 1.1 `src/HwDefine.h`：在 `HwBufferObject` 之后追加 `HwIndexBuffer`（`HwBase` 派生）；位宽以文件内 `constexpr` 表达（`kIndexCountBits = 26`、`kElementSizeBits = 5`）并在位域声明中引用；补 `kMaxElementSize = 16`；提供默认构造与 `(uint8_t elementSize, uint32_t indexCount, bool async)` 构造，后者以 `LOG_ASSERT` 约束 `elementSize ∈ [1, kMaxElementSize]` 与 `indexCount < (1u << kIndexCountBits)`
- [x] 1.2 `include/Backend/Driver.h`：public 区追加 `NODISCARD static size_t GetElementTypeSize(ElementType type) noexcept;` 声明（紧随 `Execute()` 之后，与其他公共工具成员聚拢）
- [x] 1.3 新建 `src/Driver.cpp`：include `Backend/Driver.h` 与 `Utils/Macro.h`，以 `CASE_FROM_TO` 实现 `ElementType` 全部 26 个取值的字节数映射（按 BYTE/UBYTE/SHORT/USHORT/INT/UINT/FLOAT/HALF 分组，同组分量宽度 1/2/4 展开），函数尾 `return 0;`

## 2. 驱动接口

- [x] 2.1 `include/Backend/DriverAPI.inc`：在 `CreateBufferObject` 之后插入 `DECL_DRIVER_API_TAGGED_R_N(IndexBufferHandle, CreateIndexBuffer, ElementType, elementType, uint32_t, indexCount, BufferUsage, usage)`
- [x] 2.2 `include/Backend/DriverAPI.inc`：在 `DestroyBufferObject` 之后插入 `DECL_DRIVER_API_N(DestroyIndexBuffer, IndexBufferHandle, ibh)`
- [x] 2.3 预期状态记录：本组落地后 `VulkanDriver` 会因纯虚 `CreateIndexBufferS` 未实现而不可编译，属预期；不在此处构建，待第 3、5 组完成后统一验证

## 3. VulkanIndexBuffer 资源对象

- [x] 3.1 `src/vulkan/VulkanHandle.h`：在 `VulkanBufferObject` 之后追加 `struct VulkanIndexBuffer : public HwIndexBuffer, public Resource`——构造声明 `(const VulkanContextPtr&, VmaAllocator, const VulkanBufferCachePtr&, uint8_t elementSize, uint32_t indexCount)`、`NODISCARD VkBuffer GetVkBuffer() const noexcept`、公开常量成员 `VkIndexType const indexType`、私有成员 `VulkanBufferProxy m_buffer`（声明序：public `indexType` 在 private `m_buffer` 之前）；末尾 `DECLARE_SHARE_PTR_CLASS(VulkanIndexBuffer)`
- [x] 3.2 `src/vulkan/VulkanHandle.cpp`：实现 `VulkanIndexBuffer` 构造——基类 `HwIndexBuffer(elementSize, indexCount, false)`；`indexType` 以 `elementSize == sizeof(uint16_t)` 判定 `VK_INDEX_TYPE_UINT16` / `VK_INDEX_TYPE_UINT32`；`m_buffer` 以 `VulkanBufferBinding::Index` + `BufferUsage::STATIC` + `static_cast<uint32_t>(elementSize) * indexCount` 构造

## 4. 资源层：引用语义与类型表

- [x] 4.1 `src/vulkan/resource/ResourceManager.h`：`Make` 在 `construct` 之后追加 `obj->AddRef()`（句柄持有的引用）再返回借用视图 `SharedPtr<D>`，并更新前置条件注释为"对象由句柄持有一份引用，driver 侧 `SharedPtr` 为借用视图"
- [x] 4.2 `src/vulkan/resource/ResourceManager.h`：`AllocateAndConstruct` 改为直接 `construct<D, D>(AllocHandle<D>(), std::forward<ARGS>(args)...)` 并返回拥有所有权的 `SharedPtr<D>`，不再经 `Make`
- [x] 4.3 `src/vulkan/resource/ResourceManager.h`：`Destroy` 在 `ptr.Reset()` 之后加 `LOG_ASSERT(obj->GetRefCount() >= 1)` 与 `obj->SubRef()`，释放句柄持有的引用；同步更新"driver 销毁入口"注释说明其配 `Make` 使用
- [x] 4.4 `src/vulkan/resource/Resource.h`：补前向声明 `struct VulkanBufferObject;` 与 `struct VulkanIndexBuffer;`，并在既有特化声明块中追加 `GetTypeEnum<VulkanBufferObject>` 与 `GetTypeEnum<VulkanIndexBuffer>` 两条显式特化声明
- [x] 4.5 `src/vulkan/resource/Resource.cpp`：追加两条特化定义——`GetTypeEnum<VulkanBufferObject>()` 返回 `ResourceType::BufferObject`、`GetTypeEnum<VulkanIndexBuffer>()` 返回 `ResourceType::IndexBuffer`
- [x] 4.6 `src/vulkan/resource/ResourceManager.cpp`：`destroyWithType` 增加三个分支 `ResourceType::BufferObject` / `IndexBuffer` / `VertexBuffer`（`destruct<VulkanBufferObject>` / `destruct<VulkanIndexBuffer>` / `destruct<VulkanVertexBuffer>`），与既有 `VertexBufferInfo` 分支聚拢放置

## 5. VulkanDriver 接线

- [x] 5.1 `src/vulkan/VulkanDriver.cpp`：`CreateIndexBufferS()` 返回 `m_resMgr->AllocHandle<VulkanIndexBuffer>()`，置于 `CreateBufferObjectS` 之后
- [x] 5.2 `src/vulkan/VulkanDriver.cpp`：`CreateIndexBufferR(ibh, elementType, indexCount, usage, tag)` 以 `static_cast<uint8_t>(Driver::GetElementTypeSize(elementType))` 求元素宽度，`Make<VulkanIndexBuffer>(ibh, m_context, m_allocator, m_bufferCache, elementSize, indexCount)` 后 `AssociateTagToHandle(ibh.GetId(), std::move(tag))`
- [x] 5.3 `src/vulkan/VulkanDriver.cpp`：`DestroyIndexBuffer(IndexBufferHandle ibh)` 空句柄早退 + `Acquire<VulkanIndexBuffer>` + `Destroy`，与 `DestroyBufferObject` 同形
- [x] 5.4 全量构建（`cmake --build build`）：确认 `src/Driver.cpp` 经 GLOB 自动收集（`build/CMakeFiles/Backend.dir/src/Driver.cpp.o` 生成）、`VulkanDriver` 不再有未实现纯虚方法、`CommandStream` 与 `ConcreteDispatcher` 展开通过

## 6. 测试与验证

- [x] 6.1 `tests/Engine.h` / `Engine.cpp`：增 `Backend::IndexBufferHandle CreateIndexBuffer(Backend::ElementType, uint32_t indexCount, Backend::BufferUsage)` 与 `void DestroyIndexBuffer(Backend::IndexBufferHandle)`，分别转发到 `m_stream->CreateIndexBuffer` / `m_stream->DestroyIndexBuffer`（与既有 `CreateFence` 同模式）
- [x] 6.2 `tests/App.cpp`：在帧循环内做索引缓冲创建-销毁往返，16 位（`USHORT`）与 32 位（`UINT`）各一次，覆盖 `GetElementTypeSize` 两条分支
- [x] 6.3 构建 `BackendTests` 并运行 `bin/BackendTests`：确认无 `LOG_CRITICAL` 输出、进程正常退出（exit 0）；运行需带 Vulkan 运行时环境变量：
      `DYLD_LIBRARY_PATH=/opt/homebrew/lib VK_ICD_FILENAMES=/Users/turiing/VulkanSDK/1.4.313.1/macOS/share/vulkan/icd.d/MoltenVK_icd.json ./bin/BackendTests`
      通过观察：8 帧各完成一次索引缓冲创建-销毁，`element size=2`（`USHORT`）与 `element size=4`（`UINT`）交替出现，全程无 `LOG_CRITICAL`、无断言、无崩溃
- [x] 6.4 复核类型表覆盖：`GetTypeEnum` 特化 7 条与 `destroyWithType` 分支 7 条逐一对应（`VulkanBuffer` / `BufferObject` / `IndexBuffer` / `VertexBufferInfo` / `VertexBuffer` / `StageSegment` / `Semaphore`），无遗漏
- [x] 6.5 复核引用语义（运行期无法覆盖）：逐条对照上游 `resource_ptr`——`Make` 构造后 `AddRef` 使句柄持有一份引用、返回借用视图；`AllocateAndConstruct` 不经 `Make` 故无句柄引用；`Destroy` 先 `Reset` 释放借用视图再 `SubRef` 释放句柄引用，恰好入队一次

### 7. 阻塞 6.3 的既有缺陷修复（用户决策：本变更内一并修）

- [x] 7.1 `src/vulkan/VulkanDriver.cpp`：`Create` 补 `handleArenaSize` 兜底——新增文件内常量 `kMinHandleArenaSize = 8u * 1024u * 1024u`（上游 `FVK_HANDLE_ARENA_SIZE_IN_MB = 8`），`validConfig.handleArenaSize = std::max(config.handleArenaSize, kMinHandleArenaSize)`；补 `#include <algorithm>`
      背景：测试传入 `DriverConfig{}`（`handleArenaSize = 0`）→ `ResourceManager` 以 0 字节建 arena → `FreeList.cpp:15` 越界断言，在 `VulkanDriver` 构造期即崩
- [x] 7.2 `src/vulkan/VulkanDriver.cpp`：`DestroyResources` 改为先 `Terminate()` 再 `Reset()`——`m_bufferCache->Terminate()` / `m_stagePool->Terminate()` 须在 `vmaDestroyAllocator` 之前，否则池内 VkBuffer 未归还 VMA，`vmaDestroyAllocator` 触发 `Some allocations were not freed` 断言；此约束即 `port-vulkan-buffer` design D7 已记录但未实现的生命周期要求
- [x] 7.3 `src/vulkan/VulkanDriver.cpp`：`DestroyResources` 补幂等——`terminate()` 与 `~VulkanDriver()` 都会调用它，7.2 引入的 `->Terminate()` 在第二次调用时会对已 `Reset()` 的空 `SharedPtr` 解引用（实测 `EXC_BAD_ACCESS ... __tree::begin(this=0x28)`），故每步以 `if (m_bufferCache)` / `if (m_stagePool)` 判空后成对执行

### 阻塞记录（6.3，已解除）

初次运行失败的两个原因在**基线代码**（`git stash` 掉本次全部改动后重建）上表现完全一致，确认与本次改动无关，已由第 7 组修复：

1. `volkInitialize()` 失败 → `LOG_CRITICAL` 中止（`VulkanPlatform.cpp:266`）。需要运行时环境变量才能加载 loader 与 ICD（`.vscode/launch.json` 只设了 `DYLD_LIBRARY_PATH`，缺 `VK_ICD_FILENAMES`）
2. 补上环境变量后设备可枚举（`MoltenVK 1.4.1` / `Apple M4`），但随即出现两个终止/构造期缺陷，均由第 7 组修复：0 字节 handle arena（7.1）、`DestroyResources` 未归还池内 VkBuffer 且不可重入（7.2 / 7.3）
