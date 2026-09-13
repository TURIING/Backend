# Design: port-vulkan-driver

## Context

本变更是 7 个变更的收口：把前 6 个变更交付的组件全部接进 `VulkanDriver`，使后端从「能创建设备」变为「能画出三角形」。

当前状态（探索阶段确认）：

| 项 | 现状 | 目标 |
|---|---|---|
| `VulkanDriver.cpp` | 160 行 | 约 2956 行 |
| `DriverAPI.inc` 方法数 | 15 | 143 |
| `VulkanDriver` 成员数 | 5 | 17 |
| `VulkanDriver` 基类 | `Driver` | `DriverBase` |
| 帧循环 | 全为空实现 | 完整实现 |
| `bin/BackendTests` 输出 | 8 帧创建/销毁日志 | 渲染出三角形 |

前 6 个变更已交付的组件清单（本变更的输入）：

```
变更 1  ├─ NS_UTILS::{Hash, Bitset32, Bitset64, Panic, RangeMap}
        ├─ Backend::{CallbackHandler, CallbackManager, CompilerThreadPool, DriverBase, JobSystem}
        └─ ThreadSafeResource + 双 GC 队列分类表

变更 2  ├─ DriverDefine.h 全量类型 + TargetBufferInfo/MRT
        ├─ Program 构建器
        └─ math::{float2,uint2,uint3,vec2,double4,mat3f}

变更 3  ├─ Platform 句柄族 + CompositorTiming/FrameTimestamps + DriverConfig
        └─ VulkanPlatform 交换链接口 + VulkanPlatformSwapChainImpl

变更 4  ├─ VK_UTILS::{Definitions, Conversion, Image, Spirv, Helper, StaticVector}
        ├─ VulkanMemory / VulkanTexture / VulkanSamplerCache / VulkanYcbcrConversionCache
        └─ VulkanStageImage

变更 5  ├─ VulkanConstants / VulkanContext.cpp / VulkanFboCache
        ├─ VulkanSwapChain / VulkanRenderTarget / VulkanAttachment
        └─ 渲染通道屏障发射

变更 6  ├─ VulkanAsyncHandles / VulkanPipelineCache / VulkanPipelineLayoutCache
        ├─ VulkanDescriptorSet(Layout)Cache / VulkanRenderPrimitive
        └─ VulkanQueryManager / VulkanBlitter / VulkanReadPixels
```

## Goals / Non-Goals

**Goals:**

- `DriverAPI.inc` 补齐到 143 条，`VulkanDriver` 全部实现
- `VulkanDriver` 完整生命周期：构造（含默认渲染目标预创建）→ 帧循环 → 销毁（含线程 join）
- 绘制路径打通：`beginRenderPass` → `bindPipeline` → `bindRenderPrimitive` → `draw` → `endRenderPass` → `commit`
- 25 条特性查询实现，返回值使前端能正确选择路径
- `bin/BackendTests` 渲染出三角形，中心像素颜色可校验
- 空桩方法（外部图像 / 流 / compute / 捕获）明确标注且不崩溃

**Non-Goals:**

- 不实现外部图像路径（`createTextureExternalImage*` / `importTexture*` / `setupExternalImage*`）——用户决策砍掉实现，留空桩
- 不实现视频流路径（`createStream*` / `setAcquiredImage` / `updateStreams`）——同上
- 不实现 `dispatchCompute`（上游即为 `// FIXME: implement me`）
- 不实现 `startCapture` / `stopCapture`（上游即为空）
- 不实现 Present timing 查询（MoltenVK 不支持）
- 不实现多子通道渲染（`nextSubpass` 上游有实现但本变更无测试路径）
- 不引入窗口系统——验证走 headless

## Decisions

### D1: `Driver` → `DriverBase` 的继承变更

上游 `VulkanDriver : public DriverBase`，`DriverBase` 承载 `debugCommandBegin` 钩子与 `DriverConfig` 派生字段。

**决策**：`VulkanDriver` 改为继承 `DriverBase`（变更 1 已建立该类型骨架）。

**核对项**：`DriverBase` 的构造签名须与 `VulkanDriver` 的初始化列表匹配；`Driver` 的纯虚方法（`GetDispatcher` / `terminate` / 全部同步方法）经 `DriverBase` 继承，`VulkanDriver` 的 `#include "Backend/DriverAPI.inc"` 宏展开路径不变。

**`debugCommandBegin` 的 override**：上游在 `BVK_DEBUG_*` 开关下输出命令调试信息。本项目 SHALL 保留该方法并 override，但内部改用 `LOG_DEBUG`（`utils::io::ostream` 在变更 2 中已决定不移植）。

### D2: `DriverAPI.inc` 的 143 条清单——分族批量追加

**决策**：按 proposal 的 13 个族分批追加，每批追加后立即构建，确认 `CommandStream` / `ConcreteDispatcher` 宏展开无误。

**理由**：`DriverAPI.inc` 的宏展开路径（`PAIR_ARGS_N` / `APPLY` / `DECL_DRIVER_API_*`）对参数个数敏感。一次追加 128 条若出现宏展开错误，定位成本极高；分族追加使每次失败的范围可控（约 4-17 条）。

**参数个数上限核对**：变更 1 的 spec 已确认 `PAIR_ARGS_N` / `APPLY` 支持到 13 对参数。追加时 SHALL 核对上游是否有超过 13 对的方法（若有，须先扩展宏）。

**已知的超长参数方法**：`createTexture`（`SamplerType` / `levels` / `Format` / `swizzle` / `usage` / `samples` / `width` / `height` / `depth` / `CallbackHandler*` / `Callback` / `void*` / `tag` ≈ 13 对），`createTextureViewSwizzle`、`update3DImage` 亦较长。须逐个核对。

### D3: `VulkanDriver` 构造函数中的默认渲染目标预创建

上游：

```cpp
VulkanDriver::VulkanDriver(...)
    : DriverBase(driverConfig),
      mPlatform(platform),
      mResourceManager(...),
      // 注释：We always create the default rendertarget before createDefaultRenderTarget().
      // We swap the content later when createDefaultRenderTarget() is called. This frees
      // createDefaultRenderTarget() from being ordered with makeCurrent().
      mDefaultRenderTarget(resource_ptr<VulkanRenderTarget>::construct(&mResourceManager)),
      mAllocator(createAllocator(...)),
      mContext(context),
      ...
```

**决策**：保留该设计。`mDefaultRenderTarget` 在构造期经 `AllocateAndConstruct<VulkanRenderTarget>()` 创建（无参构造），`createDefaultRenderTargetR` 只把它交给句柄。

**理由**：该顺序解除了「`createDefaultRenderTarget` 必须先于 `makeCurrent`」的约束，前端可自由排列调用顺序。这是变更 5 的 design D5 已记录的约束，本变更是其兑现点。

**实现细节**：`mDefaultRenderTarget` 的类型为 `VulkanRenderTargetPtr`；`createDefaultRenderTargetR` 内 `m_resMgr->Make<VulkanRenderTarget>(rth)` 会把新对象与预创建对象交换——须核对既有 `ResourceManager::Make` 的构造路径能否表达这个「复用已有对象」的语义。若不能，须在 `Make` 之外提供 `AssociateExisting(handle, ptr)` 形式的方法，并在本变更的 spec 中记录该扩展。

### D4: `DestroyResources` 的销毁顺序——`mReadPixels` 线程是新增约束

当前 `DestroyResources`：

```
m_resMgr->Terminate()
  → m_bufferCache->Terminate() / Reset()
  → m_stagePool->Terminate() / Reset()
  → vmaDestroyAllocator
```

本变更须扩展为 17 个组件的销毁，且**顺序有硬约束**：

```
1. m_resourceManager.Terminate()          清空 GC 队列（触发全部资源析构）
2. 各 Cache 的 Terminate()                释放 VkPipeline / VkRenderPass / VkFramebuffer /
                                          VkSampler / VkSamplerYcbcrConversion / VkDescriptorSet*
3. m_commands.Terminate()                 释放 VkCommandPool / VkSemaphore
4. m_blitter / m_queryManager.Terminate()
5. m_readPixels.Terminate()               ← 必须先于 vmaDestroyAllocator（join 读回线程）
6. m_semaphoreManager.Terminate()
7. 各池 Terminate() + Reset()（bufferCache / stagePool）
8. vmaDestroyAllocator
```

**关键约束**（变更 6 的 design D6 已记录，本变更兑现）：
- 第 5 步 SHALL 在第 8 步之前——读回线程若在 `vmaDestroyAllocator` 之后仍运行，会访问已释放的 allocator
- 每步 SHALL 判空后成对执行，使 `DestroyResources()` 幂等（`terminate()` 与析构都调用它）

**理由**：第 2 步先于第 7 步——各 Cache 的 `Terminate` 会销毁持有 `VkBuffer`/`VkImage` 的对象，若先销毁池会导致 VMA 分配泄漏断言。

### D5: 特性查询的 25 条实现——返回值决定前端路径

`is*Supported` / `get*` 系列看似简单，但返回值错误会导致前端选择不存在的路径，产生难定位的运行期错误。

**决策**：25 条查询 SHALL 逐个对照上游实现，**不允许「先返回 false/0 占位」**。

**理由**：例如 `getClipSpaceParams()` 返回 `{2.0f, -1.0f}` 或 `{2.0f, 1.0f}` 决定 Y 轴方向；`getMaxDrawBuffers()` 返回错误值会导致 MRT 越界；`isTextureFormatSupported()` 返回错误值会导致纹理创建失败。

**分类**：
- 直接查 `VulkanContext`（`isSRGBSwapChainSupported` / `isMSAASwapChainSupported` / `isStereoSupported` / `isProtectedContentSupported` 等）
- 查 `VkPhysicalDeviceLimits`（`getMaxDrawBuffers` / `getMaxTextureSize` / `getMaxUniformBufferSize` / `getMaxArrayTextureLayers` / `getUniformBufferOffsetAlignment`）
- 常量返回（`getClipSpaceParams` / `isFrameTimeSupported` / `isParallelShaderCompileSupported`）
- 查格式表（`isTextureFormatSupported` / `isRenderTargetFormatSupported` / `isTextureFormatMipmappable` / `isTextureFormatFilterable`）

### D6: 空桩方法的统一约定

被砍功能的驱动方法（外部图像 / 流）SHALL 以空桩实现，但须满足：

1. **签名完整**——`DriverAPI.inc` 中的声明不变，参数与上游一致
2. **不崩溃**——`S` 方法返回空句柄（`Handle<...>{}`），`R` 方法空实现
3. **可识别**——空桩 SHALL 以 `LOG_WARN` 或注释明确标注「未实现：外部图像路径已按设计砍掉」，使调用者能定位

**已否决的替代方案**：抛异常或 `LOG_CRITICAL` 中止。否决理由——前端（若将来接入）可能在初始化路径调用这些方法，中止会导致无法启动；空桩 + 警告更安全。

**清单**：
- `createTextureExternalImage2` / `createTextureExternalImage` / `createTextureExternalImagePlane` / `importTexture` / `importTextureAsync`
- `setupExternalImage2` / `setupExternalImage`
- `createStreamNative` / `createStreamAcquired` / `setAcquiredImage` / `setStreamDimensions` / `getStreamTimestamp` / `updateStreams` / `destroyStream` / `setExternalStream`
- `dispatchCompute`（上游即空实现，保持一致，不加警告）
- `startCapture` / `stopCapture`（上游即空）

### D7: 三角形验证用例的设计

**目标**：端到端验证完整绘制路径。

**方案**：
1. **headless 交换链**：`createSwapChainHeadless(256, 256, 0)`
2. **着色器**：内嵌最小顶点/片元 SPIR-V（顶点输出固定的三角形三顶点，片元输出纯色）
3. **管线**：无 UBO、无纹理、无描述符集——`createProgram` 传入空的 descriptor set 布局列表
4. **图元**：`createVertexBufferInfo`（1 个 buffer、1 个 attribute）→ `createVertexBuffer` → `createBufferObject` → 上传三顶点 → `createRenderPrimitive`
5. **绘制**：`beginRenderPass`（clear 为黑色）→ `bindPipeline` → `bindRenderPrimitive` → `drawArrays(0, 3, 1)` → `endRenderPass`
6. **校验**：`readPixels` 读回中心像素，断言其为片元着色器输出的颜色

**为什么可以不带描述符集**：探索阶段确认上游 `bindPipeline` 在 `layoutCount == 0` 时构造 `descriptorSetMask = 0x1`，而 `VulkanDescriptorSetCache::Commit` 会把未 stash 的位从掩码中清除，因此不绑定任何描述符集是安全的。

**SPIR-V 的来源**：需要一个最小三角形着色器的编译产物。选项：
- (a) 预先编译并作为字节数组内嵌（需一次性工具链）
- (b) 在测试运行时用 `glslang` 编译（引入新依赖）
- (c) 手写最小 SPIR-V 汇编（极难）

**决策**：采用 (a)，一次性用 Vulkan SDK 的 `glslangValidator` 编译，产物以 C 数组形式存放在 `tests/`。理由：不引入运行期依赖；产物可读可校验。

**降级方案**：若 SPIR-V 生成或管线创建在 MoltenVK 上不可行，验证降级为「清屏 + `readPixels` 校验 clear 颜色」——仍覆盖 `beginRenderPass` / `endRenderPass` / `commit` / `readPixels` 路径，只是不覆盖 `bindPipeline` / `draw`。此降级须在 tasks 中记录为显式选项。

### D8: 验证策略

1. **分阶段构建**：每追加一批 `DriverAPI.inc` 方法后构建
2. **`bin/BackendTests` 全绿**
3. **三角形用例**：中心像素颜色断言
4. **清屏降级用例**（若三角形不可行）
5. **`DestroyResources` 幂等**：连续调用 `terminate()` 两次不崩溃
6. **退出无泄漏**：`vmaDestroyAllocator` 不触发断言

**固有局限**：
- 外部图像 / 流路径**无验证**（空桩）
- `nextSubpass` / `dispatchCompute` / 捕获**无验证**
- Present timing 查询**无验证**（返回 false）
- 多帧稳定性只在 headless 下验证，不等价于有窗口的呈现路径

## Risks / Trade-offs

- [`DriverAPI.inc` 一次追加 128 条导致宏展开错误难以定位] → D2 分 13 族追加，每族后立即构建
- [参数超过 13 对的方法导致宏展开失败] → D2 要求追加前核对；若发现，先扩展 `PAIR_US_N` / `APPLY_N`
- [`VulkanDriver` 成员从 5 增至 17，构造初始化顺序错误] → 成员声明顺序须与初始化列表顺序一致；`-Wreorder` 会捕获部分错误
- [`mDefaultRenderTarget` 的预创建与 `Make` 语义冲突] → D3 记录该风险；若 `Make` 无法表达「复用已有对象」，须扩展 `ResourceManager` 并回填 `vulkan-resource` spec
- [`DestroyResources` 顺序错误导致 VMA 泄漏或线程访问已释放 allocator] → D4 固化为 8 步顺序；tasks 要求逐项核对
- [25 条特性查询返回错误值] → D5 禁止占位实现，要求逐条对照上游
- [SPIR-V 生成或管线创建在 MoltenVK 上不可行] → D7 的降级方案（清屏 + readPixels）
- [空桩方法掩盖真实调用] → D6 要求空桩带 `LOG_WARN`，使调用者能定位
- [本变更体量大（约 3000 行）] → 按 D2 的分族策略 + 组件接线分组推进；每批次独立构建

## Open Questions

- `ResourceManager` 是否需要 `AssociateExisting(handle, ptr)` 形态：`mDefaultRenderTarget` 的预创建 + 后续交给句柄的语义，用既有 `Make<D>(handle)` 是否可表达——须在实施第一步确认
- `DriverBase` 的 `debugCommandBegin` 在上游被 `VulkanDriver` 大量调用（几乎每个方法首行）；本项目是否逐处保留调用，还是仅在 `BVK_DEBUG_*` 编译开关下展开——上游用宏包裹，预计逐处保留但宏展开为空
- 三角形的 SPIR-V 生成工具链：`glslangValidator` 是否在本机 Vulkan SDK 中可用；若不可用，是否接受引入离线编译步骤
- `MaxVertexInputAttributes` 等限制的查询路径：`getMaxTextureSize(SamplerType)` 需要按 `SamplerType` 分支（2D / 3D / CUBE 各有上限），上游实现是否完全依赖 `VkPhysicalDeviceLimits`
- `VulkanDriverFactory.h` 是否需要：当前 `VulkanPlatform::CreateDriver` 直接 `new VulkanDriver`，上游用工厂函数解耦。本项目是否保持直接构造——倾向前者（保持既有接线，不引入无必要的间接层）
