# Design: port-vulkan-buffer

## Context

Filament `backend/src/vulkan/` 的 buffer 管理由三层构成：`VulkanBuffer`（GPU buffer 资源对象，持有 VkBuffer + VMA 分配，析构归还缓存池）、`VulkanBufferCache`（4 个 binding 池的 LRU 复用缓存）、`VulkanBufferProxy`（动态包装，解耦外部对 `VulkanBuffer` 的引用）。本项目 `vulkan-resource` 层（`Resource`/`ResourceManager`）已移植完毕（port-vulkan-resource tasks 1-4）：`Resource` 继承 `utils::Ref`（`OnLastRef` 虚分发注册延迟销毁）、`ResourceManager` 提供 `AllocateAndConstruct`/`Acquire`/`Destroy`/`Gc`/`Terminate`。但类型表仍是空骨架：`GetTypeEnum` 无特化、`DestroyWithType` 仅 `UNDEFINED_TYPE` 空分支，且 `construct` 模板中 `traceConstruction(GetTypeEnum<D>(), ...)` 是无对象调用（模板从未实例化故未暴露）。VMA 依赖缺失（`3rd/` 仅 Utils、volk）。VulkanDriver 仍是空壳，本次不做 driver 集成。

## Goals / Non-Goals

**Goals:**
- 完整移植 `VulkanBuffer`/`VulkanBufferCache`/`VulkanBufferProxy`（Proxy 砍 `loadFromCpu`/`referencedBy`/`VulkanStagePool`）
- 以 `Utils::SharedPtr` + `Resource` 承载引用计数，打通"SharedPtr 归零 → GC 队列 → DestroyWithType → ~VulkanBuffer 归还缓存池 → HandleAllocator 归还池块"完整销毁链
- 引入 VMA（submodule + INTERFACE target + 独立 `VMA_IMPLEMENTATION` 编译单元）
- VulkanBuffer 成为类型表第一个真实类型，验证 vulkan-resource 层可扩展性
- 全量构建 + tests 构建通过

**Non-Goals:**
- 不移植 `loadFromCpu`/`referencedBy`/`VulkanStagePool`/`resource_ptr`
- 不移植 `VulkanHandles.h` 其余 21 个类型（后续独立变更）
- 不做 VulkanDriver 集成（`VmaAllocator` 的创建方、cache 的持有方属 driver 层，后续变更）
- 不写单元测试（tests 工程依赖 driver 集成；本次以构建验证为主）

## Decisions

### D1: VMA 以 submodule + INTERFACE target + 独立编译单元接入

VMA 是单头库（`vk_mem_alloc.h`），Filament 以 submodule 引入。项目规则要求第三方库一律 submodule 存 `3rd/`，故 `git submodule add` VulkanMemoryAllocator。CMake 仿照 `volk.cmake` 建 `3rd/vma.cmake`：`add_library(vma INTERFACE)` + include 路径，追加进 `THIRD_PARTY_LIBS`。`VMA_IMPLEMENTATION` 放独立 `src/vulkan/VmaImpl.cpp`（GLOB_RECURSE 自动收集）而非塞进业务 cpp，职责单一。函数加载配置 `VMA_STATIC_VULKAN_FUNCTIONS=0` + `VMA_DYNAMIC_VULKAN_FUNCTIONS=1`：本项目用 volk 动态加载（等价于 Filament 的 bluevk），VMA 的函数指针由创建 allocator 时经 `volkGetInstanceProcAddr`/`volkGetDeviceProcAddr` 提供——但 allocator 创建属 driver 层，本次只保证库符号可链接。

### D2: 文件组织与定义归属

- `src/vulkan/buffer/` 新目录放三个类（`VulkanBuffer.h`、`VulkanBufferCache.h/.cpp`、`VulkanBufferProxy.h/.cpp`），与 `core/`/`platform/`/`resource/` 平级，按功能域分目录
- `VulkanBufferBinding` 枚举 + `VulkanGpuBuffer` 结构放 `VulkanBuffer.h`（buffer 域私有定义，遵循"定义按适用范围存放、不越层"）
- `BufferUsage` 枚举放 `include/Backend/DriverDefine.h`（driver 通用定义文件，与既有 `BackendType`/`StereoscopicType`/`GpuContextPriority` 同级；对应 Filament `backend/DriverEnums.h` 的枚举部分）

### D3: SharedPtr 替代 resource_ptr 的映射

| Filament | 本项目 |
|---|---|
| `resource_ptr<VulkanBuffer>::construct(&rm, gpuBuffer, onRecycle)` | `rm.AllocateAndConstruct<VulkanBuffer>(gpuBuffer, onRecycle)` |
| `fvkmemory::ResourceManager&` | `Backend::ResourceManager&` |
| `VulkanBuffer` 继承 `fvkmemory::Resource` | 继承 `Backend::Resource`（同构） |
| `mBuffer`（resource_ptr 成员） | `NS_UTILS::SharedPtr<VulkanBuffer>` 成员 |

`OnRecycle` 回调捕获 `this`（cache 指针），`~VulkanBuffer` 经回调归还 gpuBuffer 到池。

### D4: 销毁链与类型表扩展

```
SharedPtr<VulkanBuffer> 归零 → Ref::SubRef → Resource::OnLastRef
  → ResourceManager::DestructLaterWithType(VulkanBuffer, id)   [任意线程入队]
  → backend 线程 Gc() → DestroyWithType(VulkanBuffer, id)
  → destruct<VulkanBuffer>(Handle<VulkanBuffer>(id))
  → HandleCast + ~VulkanBuffer()（OnRecycle → cache.Release(gpuBuffer)）
  → HandleAllocator::Deallocate（归还池块）
```

`ResourceManager.cpp` include `vulkan/buffer/VulkanBuffer.h` 引入完整定义——这是类型表机制的固有特性（原版 `memory/ResourceManager.cpp` 同样 include 各 Vulkan 类型），resource 层对具体类型保持"仅 DestroyWithType 一处知晓"。`GetTypeEnum<VulkanBuffer>` 特化声明在 `Resource.h`（前向声明 `class VulkanBuffer` 即可，特化签名只涉及 `ResourceType`），定义在 `Resource.cpp`。

### D5: VulkanBufferProxy 精简形态

用户决策：保留原构造签名（去掉 `VulkanStagePool` 参数），成员保留 `mStagingBufferBypassEnabled`/`mAllocator`/`mBufferCache`/`mUsage`。代价：需要移植 `BufferUsage` 枚举 + `VulkanContext::stagingBufferBypassEnabled()` getter（目标项目均缺失）。收益：将来补移植 `loadFromCpu` 时构造签名与成员完整，零改动。`mStagePool`/`mLastReadAge` 只被已砍函数使用，不移植。`mAllocator`/`mUsage`/`mStagingBufferBypassEnabled` 当前仅存储、无读取方——保留是为签名完整，属预期（design 层面记录，非无用代码：它们是 loadFromCpu 的必需状态）。

### D6: construct 模板编译修复

`ResourceManager::construct` 中 `traceConstruction(GetTypeEnum<D>(), handle.GetId())` 以无对象方式调用私有非静态成员模板——模板未实例化时两阶段查找容忍，一旦 `AllocateAndConstruct<VulkanBuffer>` 实例化即编译失败。修复：`traceConstruction(obj->GetTypeEnum<D>(), handle.GetId())`（`ResourceManager` 是 `Resource` 的 friend，可访问私有成员；`obj` 为 `D*`，D 派生自 Resource）。

### D7: 生命周期约束

`VulkanBufferCache` 的 `OnRecycle` 回调捕获 cache 的 `this`，且缓存池为 cache 成员：**cache 必须活得比所有 `VulkanBuffer` 引用（含 GC 队列中待销毁者）更久**。约束：driver 层 terminate 顺序为"先销毁全部 buffer 引用并清空 GC 队列（`ResourceManager::Terminate`），再 `VulkanBufferCache::Terminate` + 销毁 cache"。`Gc()` 帧计数 `++` 后前 3 帧提前返回沿用原版（防无符号回绕），`TIME_BEFORE_EVICTION=3` 为局部常量。

## Risks / Trade-offs

- [VMA submodule 需联网拉取] → 固定 commit/tag；若网络不可达，任务 1.1 阻塞并报告，不绕过
- [ResourceManager.cpp 依赖具体类型定义（层次倒置）] → 类型表机制固有，与原版同构；限定在 `DestroyWithType` 单处 + 对应 include
- [GetTypeEnum 特化 vs 原版 is_same 链] → 本项目 `GetTypeEnum` 为成员模板直特化（Resource.h 既有设计，tasks 2.1 明确"特化在 Resource.cpp + 头文件声明"），VulkanBuffer 按此模式扩展；is_same 链方案（原版）改动更大且与既有实现冲突
- [Proxy 保留原签名引入的暂未使用状态（mAllocator/mUsage/…）] → 设计明确其为 loadFromCpu 的预留状态；编译期无未使用变量错误（成员非局部变量）
- [BufferUsage 无位运算符，无法 any(STATIC|SHARED_WRITE_BIT)] → 该用法属 loadFromCpu，本次不移植；将来补移植时随运算符一起引入
- [OnRecycle 捕获 this 的悬垂风险] → D7 生命周期约束 + code-review 检查 terminate 顺序

## Open Questions

- `VmaAllocator` 的创建方与 `VulkanBufferCache`/`VulkanBufferProxy` 的持有方：属 VulkanDriver 集成（后续变更）。本次仅要求库符号可链接，`createAllocator` 助手（volk 函数指针）留待该变更
- `VulkanBufferProxy` 是否需要在后续补 `loadFromCpu`/`referencedBy`（连同 `VulkanStagePool` 一起）——按用户指示本次不移植，留待 stage 相关变更
- `VulkanBuffer::GetCount()`（引用计数查询）原版用于 loadFromCpu 的可用性判断——不移植 loadFromCpu 则不需要，暂不添加
