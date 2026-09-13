# Change: port-vulkan-texture

## Why

纹理层是交换链的强制依赖——`VulkanSwapChain` 构造时会给颜色和深度附件各造一个 `VulkanTexture`（上游 `VulkanSwapChain.cpp:93/102`），而 `VulkanAttachment` 的 `getImage` / `getFormat` / `getLayout` / `getImageView` 全部挂在该 texture 上。**不做纹理层，交换链与渲染目标都无从谈起。**

本变更同时收口三类支撑件：

1. **类型转换工具**（`src/vulkan/utils/`，约 1870 行）：`Conversion`（`TextureFormat` → `VkFormat` 穷举映射、布局转换、混合因子/比较函数/环绕模式映射）、`Definitions`（全量 `VkFormat` 列表、位掩码类型）、`Image`（图像布局、像素拷贝）、`Spirv`（SPIR-V 校验）、`Helper`、`StaticVector`。这些是上游 Vulkan 组件引用密度最高的一层。
2. **VMA 图像分配**（`VulkanMemory`，107 行）：图像内存的分配与释放封装。
3. **采样器与 YCbCr 转换缓存**：`VulkanSamplerCache`（135 行）与 `VulkanYcbcrConversionCache`（148 行）。用户决策为**保留 Ycbcr**——代价仅 148 行，收益是 `VulkanDriver` 里所有 `mYcbcrConversionCache` 调用点保持原样，不必改动上游实现。

## What Changes

### 1. `src/vulkan/utils/` 工具层（新增 6 个文件）

| 文件 | 上游行数 | 内容 |
|---|---|---|
| `Definitions.h` | 461 | `VkFormatList`、全量 `VK_FORMAT_*` 数组、`DescriptorSetMask` / `UniformBufferBitmask` / `SamplerBitmask` 等位掩码类型 |
| `Conversion.h` / `.cpp` | 116 + 约 1107 | `GetVkFormat` / `IsVkDepthFormat` / `IsVkStencilFormat` / `GetBlendFactor` / `GetCompareOp` / `GetStencilOp` / `GetWrapMode` / `GetFilter` / `GetCullMode` / `GetFrontFace` / `GetPrimitiveTopology` / `getSwizzleFilament` / `TransitionLayout` 等 |
| `Image.h` / `.cpp` | 123 + 304 | `VulkanLayout` 枚举、`GetImageAspect`、`TransitionLayout`、`ReduceSampleCount`、像素拷贝辅助 |
| `Spirv.h` / `.cpp` | 49 + 131 | SPIR-V 二进制校验与着色器模块创建辅助 |
| `Helper.h` | 85 | 头文件级小工具（`Hash` 特化、结构体判等） |
| `StaticVector.h` | 135 | 小容量静态向量（`VulkanAwaitHandles` / `VulkanProgram::BindingList` 使用） |

`StaticVector` SHALL 保留为独立类型（不用 `std::vector` 替代）——它被用作**命令流命令体成员**，要求无堆分配。

### 2. `VulkanMemory`（107 行）

- `VulkanMemory`：`VkImage` 的 VMA 分配/释放封装，提供 `AllocateImage` / `FreeMemory` / `GetMemory`
- 与既有 `VulkanBuffer` / `VulkanBufferCache` 的分工：前者管 `VkImage`，后者管 `VkBuffer`

### 3. `VulkanTexture`（1236 行，全部移植）

上游 `VulkanTexture` 有 4 个构造分支：
1. 从零创建（`createTextureR` 路径）
2. 从已存在的 `VkImage` 包装（**交换链路径**，`VulkanSwapChain.cpp:93/102` 用它）
3. 从 AHardwareBuffer / 外部图像创建（**砍**）
4. 从 `VkImage` + 自定义 `VkImageView` 创建（texture view / swizzle 路径）

**裁掉的分支**：构造分支 3、`VulkanStream` 内联类型、`mStream` 成员、`mYcbcr` 成员中的外部格式路径、`setExternalStream` 相关方法。保留 `Ycbcr` 结构本身（`VulkanYcbcrConversionCache` 需要）。

功能面 SHALL 完整保留：`LoadImage` / `GenerateMipmaps` / `SetLinearTileMode` / `GetAttachment` / 布局跟踪（`RangeMap`）。

### 4. `VulkanSamplerCache`（135 行）

- `tsl::robin_map` → `std::unordered_map`（按 `backend-utils` 的映射约定，**须核对 `erase` 语义**）
- 键为 `SamplerParams` + `VkSamplerYcbcrConversion` 句柄的组合

### 5. `VulkanYcbcrConversionCache`（148 行）

- `tsl::robin_map` → `std::unordered_map`
- 依赖 `VulkanPlatform::ExternalYcbcrFormat`（变更 3 已保留）

### 6. `VulkanStagePool` 补 `VulkanStageImage`（**BREAKING**：既有文件扩展）

上游 `VulkanStagePool.h` 第 131-190 行定义 `VulkanStageImage`（图像暂存），既有本项目的 `VulkanStagePool` 移植时砍掉了它。本变更回补：

- `class VulkanStageImage`：持 `VkImage` / `VmaAllocation` / `VkFormat` / `width` / `height` / `lastAccessed`，内含 `Resource` 回收回调
- `VulkanStagePool::AcquireStageImage(VkFormat, uint32_t width, uint32_t height)`：按格式与尺寸复用暂存图像
- `ResourceType::StageImage` 枚举已在 `Resource.h` 预留（值 16），本变更补 `GetTypeEnum` 特化与 `DestroyWithType` 分支

## Capabilities

### New Capabilities

- `vulkan-conversion`: `Definitions` / `Conversion` / `Image` / `Spirv` / `Helper` / `StaticVector`
- `vulkan-memory`: `VulkanMemory` 图像内存封装
- `vulkan-texture`: `VulkanTexture` / `VulkanSamplerCache` / `VulkanYcbcrConversionCache`

### Modified Capabilities

- `vulkan-stage-buffer`: 新增 `VulkanStageImage` 与 `AcquireStageImage`；`ResourceType::StageImage` 的类型表与销毁分支

## Impact

- 新增：
  - `src/vulkan/utils/Definitions.h`
  - `src/vulkan/utils/Conversion.h` / `.cpp`
  - `src/vulkan/utils/Image.h` / `.cpp`
  - `src/vulkan/utils/Spirv.h` / `.cpp`
  - `src/vulkan/utils/Helper.h`
  - `src/vulkan/utils/StaticVector.h`
  - `src/vulkan/VulkanMemory.h` / `.cpp`
  - `src/vulkan/VulkanTexture.h` / `.cpp`
  - `src/vulkan/VulkanSamplerCache.h` / `.cpp`
  - `src/vulkan/VulkanYcbcrConversionCache.h` / `.cpp`
- 修改：
  - `src/vulkan/stage/VulkanStagePool.h` / `.cpp`（+`VulkanStageImage` + `AcquireStageImage`）
  - `src/vulkan/resource/Resource.h` / `.cpp`（+`GetTypeEnum<VulkanStageImage>` 特化）
  - `src/vulkan/resource/ResourceManager.cpp`（+`StageImage` 销毁分支）
  - `src/vulkan/VulkanContext.h`（+`VulkanAttachment`，若变更 5 不移走该类型则本变更落位）
- CMake：新增源文件由 `file(GLOB_RECURSE src/*.cpp)` 自动收集
- 不改：`VulkanBufferCache` / `VulkanBufferProxy`（缓冲路径已存在）
- 验证：`bin/BackendTests` 不回归；新增 `VulkanStageImage` 的分配-回收往返测试与 `GetVkFormat` 的枚举穷举编译期检查（`-Wswitch`）
