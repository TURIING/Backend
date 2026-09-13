# VulkanDriver 移植路线图

本文件是 7 个关联变更的索引。它们把上游 `filament/backend/src/vulkan/VulkanDriver.cpp`（2956 行 / 143 个驱动方法）移植到本项目，**必须按序推进**——每个变更都是后一个的前置。

## 变更序列

| # | 变更 | 核心产出 | 前置 |
|---|---|---|---|
| 1 | `port-vulkan-foundation` | `Hash`/`Bitset`/`Panic`/`RangeMap`；`CallbackHandler`/`CallbackManager`/`CompilerThreadPool`/`DriverBase`；`ThreadSafeResource` + 双 GC 队列 | — |
| 2 | `port-vulkan-enums` | `DriverDefine.h` 全量类型 + `TargetBufferInfo`/`MRT`；`Program` 构建器；`math::` 6 类型 | 1 |
| 3 | `port-vulkan-platform-swapchain` | `Platform` 句柄族与 `DriverConfig`；`VulkanPlatform` 交换链接口；`VulkanPlatformSwapChainImpl` | 2 |
| 4 | `port-vulkan-texture` | `VK_UTILS::{Definitions,Conversion,Image,Spirv,Helper,StaticVector}`；`VulkanMemory`/`VulkanTexture`/`VulkanSamplerCache`/`VulkanYcbcrConversionCache`；`VulkanStageImage` | 2, 3 |
| 5 | `port-vulkan-rendertarget` | `VulkanConstants`/`VulkanContext.cpp`/`VulkanFboCache`；`VulkanSwapChain`/`VulkanRenderTarget`/`VulkanAttachment` | 4 |
| 6 | `port-vulkan-pipeline` | `VulkanAsyncHandles`/`VulkanPipelineCache`/`VulkanPipelineLayoutCache`/`VulkanDescriptorSet(Layout)Cache`；`VulkanQueryManager`/`VulkanBlitter`/`VulkanReadPixels` | 4, 5 |
| 7 | `port-vulkan-driver` | `DriverAPI.inc` 补齐 143 条；`VulkanDriver` 主体；三角形验证 | 1–6 |

依赖图：

```
  1 foundation ──► 2 enums ──┬──► 3 platform/swapchain ──► 4 texture ──┬──► 5 rendertarget ──┐
                             │                                         │                     ├──► 7 driver
                             └─────────────────────────────────────────┴──► 6 pipeline ──────┘
```

## 跨变更的既定决策

以下决策在某个变更中确立，后续变更直接引用，**不得重新决策**：

| 决策 | 确立于 | 内容 |
|---|---|---|
| `tsl::robin_map` → `std::unordered_map` | 1（design D7） | 7 个使用文件逐个替换；**保留自定义 Hash 与 Equal**；替换前须核对 `erase` 语义三问 |
| `utils::` 类型映射表 | 1（`backend-utils` spec） | `Invocable`→`std::function`、`Condition`→`std::condition_variable`、`Mutex`→`std::mutex`、`CString`→`NS_UTILS::String`、`FixedCapacityVector`→`std::vector`、`JobSystem`→空实现 |
| `fvkutils` → `VK_UTILS` | 4（design D3） | 与既有 `VkUtils.h` 合并为同一命名空间 |
| `BlueVK` → `volk` | 1 | 删除 `using namespace bluevk;` |
| 方法名保留项目拼写 | 探索阶段 | `SetVertexBufferObject` 永久不改；新方法用 `PascalCase` |
| `math::` 命名空间与类型名保留上游拼写 | 2（design D2） | 上游调用点零改写 |
| 枚举值名保留上游 `UPPER_SNAKE_CASE` | 2（适配约束） | `TextureFormat` / `VulkanLayout` 等逐值引用 |
| **新引入的通用容器一律用项目 `PascalCase`** | 1（design D1） | `Bitset` / `RangeMap` / `Range` / `hash` 的类型与公有方法；变更 4–7 移植上游调用点时须机械改名（清单见变更 1 的 tasks 6.8） |
| 断言宏 `FILAMENT_CHECK_*` / `assert_invariant` 保留上游名 | 1（design D2） | 两者语义不同：`FILAMENT_CHECK_*` release 生效且支持 `<<`；`assert_invariant` debug-only 且不支持 |
| 砍掉外部图像与视频流 | 探索阶段 | `VulkanExternalImageManager` / `VulkanStreamedImageManager` 不移植；驱动方法留空桩 |
| 保留 `VulkanYcbcrConversionCache` | 探索阶段 | 148 行成本低，使 driver 调用点保持上游原样 |
| 只做 macOS / MoltenVK | 探索阶段 | `#ifdef __ANDROID__` 分支整体删除 |
| 不移植 `JobQueue` | 1（design Context） | Vulkan 后端零引用 |
| 不移植 `utils::io::ostream` | 2（design D4） | 改用 `LOG_*` |

## 已记录的待验证项

以下事项在某个变更中明确标注「未验证」，须在后续变更兑现：

| 待验证项 | 记录于 | 兑现于 |
|---|---|---|
| `ThreadSafeResource` 的归零入队路径 | 1（tasks 6.6） | 6（tasks 10.3） |
| `VulkanPipelineCache` 的并行编译并发入队 | 6（design D7） | 7（若无法触发则保持未验证并记录） |
| `EmitBarriersBeginRenderPass` / `EndRenderPass` 的运行期行为 | 5（tasks 8.9） | 7（三角形用例） |
| `VulkanTexture::LoadImage` / `GenerateMipmaps` | 4（tasks 9.8） | 7 |
| 有窗口的呈现路径（`Acquire` / `Present`） | 3, 5 | 未规划（当前只做 headless） |

## 关键约束速查

- **销毁顺序**（变更 7）：`ResourceManager::Terminate` → 各 Cache `Terminate` → `Commands::Terminate` → `Blitter` → **`ReadPixels`（join 线程）** → `SemaphoreManager` → 各池 `Terminate`+`Reset` → `vmaDestroyAllocator`
- **`DriverAPI.inc` 分批追加**（变更 7）：按 13 个族逐批追加并构建，宏展开错误定位成本随批量线性上升
- **提交拆分**：变更 1 含 `3rd/Utils` 子模块改动，须在子模块内单独提交，本项目仓库记录指针更新
- **运行测试的环境变量**：
  ```sh
  DYLD_LIBRARY_PATH=/opt/homebrew/lib \
  VK_ICD_FILENAMES=/Users/turiing/VulkanSDK/1.4.313.1/macOS/share/vulkan/icd.d/MoltenVK_icd.json \
  ./bin/BackendTests
  ```

## 推进方式

每个变更独立走 `/har:apply` → 验证 → `/har:archive`。**不要并行推进**——变更 N+1 的类型依赖变更 N 的产出，并行会导致反复返工。

变更 7 完成后，`bin/BackendTests` 应能渲染出三角形。
