# Design: port-vulkan-stage-buffer

## Context

上游 `VulkanStagePool.h/.cpp` 实际装了三样东西：`VulkanStage`（可切分的共享 CPU-GPU 暂存缓冲）、`VulkanStageImage`（linear tiling 的暂存图像 + 其 `Resource` 包装）、`VulkanStagePool`（两者的复用池）。三者只共享一个池对象，依赖面差异极大：

- `VulkanStage` + `Segment` 只依赖 VMA、`ResourceManager`、`VkPhysicalDeviceLimits`——本项目全部具备
- `AcquireImage` 额外依赖 `VulkanCommands::get().buffer()`（拿录制中的命令缓冲）、`fvkutils::transitionLayout`（`vulkan/utils/Image.h` 的 `VulkanLayout` 体系）、`fvkutils::getVkFormat(PixelDataFormat, PixelDataType)`（`backend/DriverEnums.h` 的像素枚举）——本项目三者皆无，且 `VulkanDriver` 仍是空壳

本项目 `vulkan-resource` 层已就绪，`port-vulkan-buffer` 已打通「`SharedPtr` 归零 → GC 队列 → `DestroyWithType` → `~T` 触发 OnRecycle → 归还池块」的完整销毁链，并已预埋 `ResourceType::StageSegment(15)`/`StageImage(16)` 的枚举与字符串映射。本设计只覆盖缓冲侧。

## Goals / Non-Goals

**Goals:**

- 完整移植 `VulkanStageBuffer`（含嵌套 `Segment`）与 `VulkanStagePool` 的**缓冲路径**，算法与上游保持一致（容量排序复用、跨帧淘汰、非相干原子对齐）
- 以 `NS_UTILS::SharedPtr<Segment>` + `Resource` 承载切分单元的生命周期，`Segment` 析构经 OnRecycle 回调在父缓冲中摘除自身条目
- 让 `VulkanStageBuffer::Segment` 成为类型表中第一个**嵌套类型**资源，验证 `AllocateAndConstruct` 对嵌套类型的可用性
- 让 `VulkanStageBuffer` 的独占所有权按项目智能指针约定表达（`: NS_UTILS::Ref` + 池持 `UniquePtr`），消除手工 `new`/`delete`，并把 VMA 释放内聚到对象析构
- 全量构建 + tests 构建通过

**Non-Goals:**

- 不移植 `VulkanStageImage`、`AcquireImage`、`mFreeImages`、`gc()`/`terminate()` 的图片分支
- 不移植 `PixelDataFormat`/`PixelDataType`/`getVkFormat`、`VulkanLayout`/`getVkLayout`/`transitionLayout`/`getImageAspect`
- 不引入 `VulkanCommands` 或任何命令录制抽象
- 不补 `VulkanBufferProxy::loadFromCpu`，不做 driver 集成（`VmaAllocator` 创建方、池持有方、`Gc()` 帧循环调用方均属后续变更）
- 不写单元测试（tests 工程依赖 driver 集成，本次以构建验证为准，与 `port-vulkan-buffer` 口径一致）

## Decisions

### D1: 范围裁剪——image 路径整块延后

`AcquireImage` 的三处缺口（命令录制、布局转换、像素格式映射）每一项都是独立能力域，且相互依赖（`transitionLayout` 的调用前提是有一个录制中的命令缓冲）。把它们塞进本次会让变更从「移植两个类」膨胀成「先造半层 Vulkan 基础设施」。

代价：`VulkanStagePool` 会先以「纯 buffer 池」形态存在，将来补 image 时需要**回到这个类**加 `mFreeImages`、`AcquireImage` 与 `gc()`/`terminate()` 的图片分支——属于对同一文件的功能追加，不是重写。

考虑过的替代方案：先把 `VulkanStageImage` 类定义搬过来、只留 `AcquireImage` 不实现——否决，类定义单独存在无任何价值，且 `VulkanStageImage` 的唯一意义就是 `AcquireImage` 的产出。

### D2: 文件组织——`src/vulkan/stage/` 内按类分文件

```
src/vulkan/stage/
├── VulkanStageBuffer.h/.cpp   // VulkanStageBuffer + 嵌套 Segment + GetTypeEnum 特化
├── VulkanStagePool.h/.cpp     // 池（纯 buffer 形态）
└── VulkanStageImage.h/.cpp    // 将来：本次不创建
```

与 `buffer/`、`core/`、`platform/`、`resource/` 的分域方式一致。代价是上游同文件里的 `friend class VulkanStagePool` 关系被拆到两个头文件（本设计不依赖 friend：`VulkanStageImage::mLastAccessed` 那种跨类私有写入在本次范围内不存在），以及 `VulkanStageBuffer::Segment` 的 `GetTypeEnum` 特化声明与 `VulkanStagePool` 分处两个文件。

### D3: Segment 的 Resource 化与嵌套类型特化落位

| 上游 | 本项目 |
|---|---|
| `resource_ptr<Segment>::construct(resManager, this, numBytes, offset, onRecycle)` | `resourceManager.AllocateAndConstruct<VulkanStageBuffer::Segment>(this, numBytes, offset, onRecycle)` |
| `fvkmemory::resource_ptr<Segment>` 返回值 | `NS_UTILS::SharedPtr<VulkanStageBuffer::Segment>` |
| `Segment : fvkmemory::Resource` | `Segment : Backend::Resource`（同构） |

`GetTypeEnum` 特化的落位是本设计唯一的机制性偏离：本项目现有模式是「`Resource.h` 里前向声明 + 声明特化，`Resource.cpp` 里定义」（`VulkanBuffer`、`VulkanVertexBufferInfo` 均是）。但 `Resource.h` 只做 `class VulkanStageBuffer;` 的前向声明时**写不出** `VulkanStageBuffer::Segment`，因此特化声明改放 `VulkanStageBuffer.h` 的类定义之后、定义放同名 `.cpp`。

风险点在**可见性时机**：主模板对未知类型返回 `UndefinedType` 而非编译错误，若特化声明晚于 `AllocateAndConstruct<Segment>` 的实例化点就会静默退化（只在 `LOG_ASSERT(type != UndefinedType)` 处暴露）。本设计的实例化点是 `VulkanStageBuffer.cpp` 的 `AcquireSegment`，特化声明位于该 TU 首个 include 的头文件中、早于实例化点，按 [temp.expl.spec]/6 必然被选中（落地后已用一次性最小程序实测确认解析为 `StageSegment`，未退回主模板）。

考虑过的替代方案：把 `Segment` 提升为顶层类 `VulkanStageSegment`，从而能塞回 `Resource.h`——否决，命名与上游偏离，且「`Segment` 依附于某个 `VulkanStageBuffer`」这层关系由嵌套表达最自然。

### D4: 构造签名改按本项目依赖传递方式

上游 `VulkanStagePool(VmaAllocator, fvkmemory::ResourceManager*, VulkanCommands*, const VkPhysicalDeviceLimits*)`，本项目改为 `(VulkanContext const&, ResourceManager&, VmaAllocator)`：

- `VulkanCommands*` 去掉（唯一使用者是 `AcquireImage`，见 D1）
- `const VkPhysicalDeviceLimits*` → `VulkanContext const&`，非相干原子大小经 `m_context.GetPhysicalDeviceLimits().nonCoherentAtomSize` 现取
- `ResourceManager*` → `ResourceManager&`（与 `VulkanBufferCache` 一致，非空是构造前置条件）

### D5: VMA 内存用法对齐本项目 `VulkanBufferCache`

上游用 legacy 的 `VMA_MEMORY_USAGE_CPU_ONLY` + `vmaMapMemory`/`vmaUnmapMemory` 手工映射。本项目 `VulkanBufferCache` 已改用 VMA 3.x 风格，stage buffer 跟随后者：

```
VkBufferCreateInfo   { usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, size = alignToNonCoherentAtomSize(capacity) }
VmaAllocationCreateInfo { .flags = MAPPED_BIT | HOST_ACCESS_SEQUENTIAL_WRITE_BIT, .usage = VMA_MEMORY_USAGE_AUTO }
VmaAllocationInfo       → pMappedData 作为持久映射指针存入 VulkanStageBuffer
```

收益：映射随分配一次完成，销毁路径只需 `vmaDestroyBuffer`（少一对 map/unmap 调用与失败分支）；`.flags` 显式声明 host 访问意图，比 `CPU_ONLY` 这种「用途+位置」耦合的 legacy 值更贴合 VMA 3 的语义。注意**不得**照搬 `VulkanBufferCache` 的 `requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`——暂存缓冲要的是 host 可见内存。

### D6: 上游算法与常量原样保留

`AcquireStage` 的 multimap 键语义（新缓冲以 `capacity` 入表、复用后以 `capacity - currentOffset` 回插）、扫描条件（`segmentOffset >= currentOffset()` 防溢出 且 `capacity - numBytes >= segmentOffset` 判容量）、`Gc()` 的「前 3 帧提前返回 + 空缓冲最多留 1 个 + `Reset()` 后以满容量回插」策略均照抄，避免行为漂移。

`alignValue(value, alignment)` 保留为 stage 模块内的文件级 helper，**不**替换成 `CommandStream.h` 里的 `ALIGN_UP`：后者是命令流模块的私有定义（通用定义散落问题另记 Open Questions），且只对 2 的幂对齐正确、不处理 `alignment == 0`，而 `nonCoherentAtomSize` 之外的调用方传入的对齐值不受此保证。

常量落位：`kStageSize = 1 << 20`、`kMaxEmptyStagesToRetain = 1`、`kTimeBeforeEviction = 3` 收进 `.cpp` 匿名命名空间（上游 `TIME_BEFORE_EVICTION` 定义在文件顶层、另两个在匿名命名空间，本次统一）。

### D7: 命名与适配映射

| 上游 | 本项目 |
|---|---|
| `VulkanStage` | `VulkanStageBuffer`（用户指定改名） |
| `memory()`/`buffer()`/`capacity()`/`mapping()`/`currentOffset()` | `GetMemory()`/`GetVkBuffer()`/`GetCapacity()`/`GetMapping()`/`GetCurrentOffset()` |
| `isSafeToReset()`/`reset()`/`acquireSegment()` | `IsSafeToReset()`/`Reset()`/`AcquireSegment()` |
| `acquireStage()`/`allocateNewStage()`/`destroyStage()`/`gc()`/`terminate()` | `AcquireStage()`/`allocateNewStage()`/`destroyStage()`/`Gc()`/`Terminate()` |
| `assert_invariant` / `UTILS_UNUSED` | `LOG_ASSERT` / `(void)` |
| `FVK_ENABLED(FVK_DEBUG_STAGING_ALLOCATION)` | `BVK_ENABLED(BVK_DEBUG_STAGING_ALLOCATION)`（`VkDef.h` 已定义） |
| `FVK_SYSTRACE_CONTEXT/START/END` | 不移植（本项目无 systrace 宏） |
| `fvkmemory::ResourceManager*` | `Backend::ResourceManager&` |
| `new VulkanStageBuffer(...)` / `delete stage` + 池内 `vmaDestroyBuffer` | `NS_UTILS::MakeUnique<VulkanStageBuffer>(...)` / `UniquePtr::Reset()`；`vmaDestroyBuffer` 移入 `~VulkanStageBuffer`（D10） |

`AcquireStage`/`Gc`/`Terminate` 与私有 helper 一律 `noexcept`，与 `VulkanBufferCache::Acquire/Gc/Terminate` 保持一致（该处同样在 `noexcept` 函数内做堆分配）。

### D8: 生命周期与线程约束

`Segment` 持有父 `VulkanStageBuffer*` 裸指针，`OnRecycle` 回调也捕获该指针；`VulkanStagePool` 以 `UniquePtr` multimap 独占持有全部缓冲。因此：

- **池 SHALL 比其产出的全部 Segment（含 GC 队列中待析构者）存活更久**：调用方顺序为 `ResourceManager::Terminate()`（排空 GC 队列）→ `VulkanStagePool::Terminate()` → 销毁池。选 A 形态（D10）不改变这条——`Segment` 仍是借用者
- `Gc()` 只销毁 `IsSafeToReset()` 为真的缓冲，而 `IsSafeToReset()` 等价于「该缓冲无存活 Segment」，故淘汰与 Segment 存活天然互斥
- **池 SHALL 先于 `VmaAllocator`/`VkDevice` 析构**（A1 新增）：`m_stages` 是池的成员，池一销毁它必然析构并逐个释放 `UniquePtr`，从而触发 `vmaDestroyBuffer`。这既是收益（忘记 `Terminate` 不再泄漏 VkBuffer），也是新的硬约束——释放时机改由 C++ 对象生命周期决定，而不再由池内某次显式调用决定
- `AcquireStage` 非线程安全，仅驱动线程调用（上游同此约定）

### D9: 验证策略

本变更的产物在仓库内**没有调用方**（`loadFromCpu` 未移植、driver 帧循环未接），因此验证手段是全量构建 + `BackendTests` 无回归；spec 中所有运行期 Scenario 属契约描述，实际验证推迟到 `loadFromCpu` 与 driver 接线变更。这是刻意的取舍：`VulkanBufferCache`/`VulkanBufferProxy` 也是以同样方式先落地再接线。

### D10: 用 Utils 智能指针表达独占所有权（A 形态）

上游以裸 `new`/`delete` 管理 `VulkanStageBuffer`，而项目代码审查约定（`review.local.md` 第 3 条）要求「能包就优先用 `3rd/Utils/include/Utils/mem` 里的智能指针」。但该目录下两个指针都带 `static_assert(std::is_base_of_v<Ref, T>)`（`UniquePtr.h:17`），没有能包住普通对象的智能指针——所以这道选择题真正的前置题是「要不要让 `VulkanStageBuffer` 继承 `Ref`」。

结论：继承 `Ref`，池持 `UniquePtr`。所有权本来就是独占的，`UniquePtr` 的定位（「独占所有权，与 SharedPtr 相对」）与之精确对应；`SharedPtr` 的共享语义没有真实需求——`Segment` 对父缓冲是**借用**而非共持，而借用之所以成立，是因为带活 Segment 的缓冲永远不会被回收（`Gc()` 只淘汰 `IsSafeToReset()` 为真者）。唯一可达的违约路径是 release 构建下带着活 Segment 调 `Terminate()`，此时 debug 的 `LOG_ASSERT` 已先响。

| 备选 | 处置 |
|---|---|
| B 仅 `Ref` + `SharedPtr`（`Segment` 也持 `SharedPtr`） | 否决：封住悬垂的同时，把「只淘汰空缓冲」从防护降级为正确性前提，收益不抵风险；将来真要封应直接上 C |
| C `Resource` + GC 队列 | 本次否决：需新增上游没有的 `ResourceType::StageBuffer`，与 `vulkan-resource` 的「完整移植 22 种 Filament 类型」需求冲突；且 teardown 顺序要从「`ResourceManager::Terminate()` → 池 `Terminate()`」翻转为「池 `Terminate()` → `ResourceManager::Terminate()`」。待 `Segment` 真正被命令缓冲跨帧长期持有时再评估 |
| D `std::unique_ptr` | 否决：无需 `Ref` 且零尺寸开销，但违反审查约定的字面，并与项目其余智能指针家族不一致 |

**子决策 A1**：`~VulkanStageBuffer` 内调 `vmaDestroyBuffer`。`UniquePtr` 不支持自定义删除器，Vulkan 释放只能内聚在对象里；若改由池的 `Terminate()` 显式释放、析构只 `delete this`（A2），`UniquePtr` 就只包住内存而没包住资源，且忘记 `Terminate` 仍漏 VkBuffer。A1 的代价即 D8 新增的池析构顺序约束。

**实测尺寸**（DWARF，`VulkanStageBuffer.cpp.o`）：现状 80 B（`0x50`）；加 `Ref`（vptr + 原子计数）后 96 B，再加 `VmaAllocator` 8 B 后 104 B，仍落在 `HandleAllocator<64,160,312>` 的 160 桶内（与 `Segment` 同桶）。`Segment` 尺寸不变（`SharedPtr`/`UniquePtr` 与裸指针同为 8 B）。

迁移路径：A → B 只需换持有者（`UniquePtr` → `SharedPtr`，`Segment` 改持 `SharedPtr`），基类不用动；只有升到 C 才需要 `Resource` 与枚举项。因此 A 是低后悔选择，C 才是单向门。

## Risks / Trade-offs

- [嵌套类型特化的可见性时机：声明晚于实例化点会静默返回 `UNDEFINED_TYPE`] → 特化声明放 `VulkanStageBuffer.h` 类定义之后，实例化点（`VulkanStageBuffer.cpp` 的 `AcquireSegment`）与声明同 TU 且声明在前；debug 下 `traceConstruction` 的 `LOG_ASSERT(type != UndefinedType)` 兜底；落地后已用一次性最小程序实测确认
- [multimap 键语义抄错导致复用失效或容量误判] → 严格按上游「新建以 capacity 入表、复用后以剩余空间回插」实现，spec 的「池内复用」「池内不足时新建」两个 Scenario 作为复核清单
- [池先于 Segment 销毁 → 悬垂父缓冲指针] → D8 约束；driver 接线变更需按序 `Terminate`，在 `port-vulkan-buffer` 的 D7 已记录同类约束。该风险**不受 A 形态影响**（`Segment` 仍是借用者），B/C 才能封住
- [RAII 与设备销毁顺序：释放时机改由 C++ 对象生命周期决定] → 池 SHALL 先于 `VmaAllocator`/`VkDevice` 析构（D8）；风险在于「忘记 `Terminate`」从惰性泄漏变成设备死后的 Vulkan 调用，后者更难定位，故 driver 接线时须把池放在设备生命周期之内
- [VMA 3.x 写法与上游 legacy 写法行为差异（如 `pMappedData` 为空）] → `HOST_ACCESS_SEQUENTIAL_WRITE_BIT` + `MAPPED_BIT` 组合下 VMA 保证分配落在 host 可见内存并预映射；若映射为空属驱动能力问题，`Segment::GetMapping()` 返回空指针会在使用处暴露
- [无运行期调用方 → 缺陷只能靠编译与人工复核发现] → 明确记入 Non-Goals；把「运行期验证」列为后续 `loadFromCpu` 变更的验收项
- [规则层面：`alignValue` 与 `CommandStream.h` 的 `ALIGN_UP` 形成同义重复] → 本次不改动既有模块（避免牵连命令流），记入 Open Questions，待通用宏上移时一并处理

## Open Questions

- `kStageSize` 是否需可配置：上游注释自述「temporary values, they will be configurable」，本项目是否引入 `DriverConfig` 字段留待有真实调参需求时决定；本次按常量
- `ALIGN_UP` 是否上移到项目统一宏文件（现私有定义在 `CommandStream.h`）：涉及既有模块改动，本次不动
- 将来补 image 路径时 `AcquireImage` 的布局转换契约怎么定（显式传 `VkCommandBuffer` vs 引入最小 `VulkanCommands` 抽象）——本次探索已列出三个方案，留待 `VulkanCommands` 变更一并决策
- `VulkanStagePool` 是否需要同时承担 `VulkanStageImage` 之外的其它 staging 需求（如内存映射缓冲 `MemoryMappedBuffer`）——观察后续移植
- 何时从 A 升到 C：当 `Segment` 开始被命令缓冲跨帧长期持有、且希望把 `Terminate` 的调用契约升级为结构保证时；届时需一并处理新枚举项与 teardown 顺序翻转
