# 变更 4 拆分说明

`port-vulkan-texture` 是路线图中体量最大的一块（约 3000 行）。实施时拆为两个连续批次，每批独立验收：

## 批次 A：`src/vulkan/utils/` 工具层（约 1900 行）

| 文件 | 上游行数 | 内容 |
|---|---|---|
| `Definitions.h` | 461 | `VkFormatList`、全量 `VK_FORMAT_*` 数组、`DescriptorSetMask` / `UniformBufferBitmask` / `SamplerBitmask` |
| `Conversion.{h,cpp}` | 116 + 1107 | `GetVkFormat` 等 15 个枚举映射 + `TransitionLayout` |
| `Image.{h,cpp}` | 123 + 304 | `VulkanLayout` 枚举、布局转换、`ReduceSampleCount` |
| `Spirv.{h,cpp}` | 49 + 131 | SPIR-V 校验 + `VkShaderModule` 创建辅助 |
| `Helper.h` | 85 | 头文件级小工具 |
| `StaticVector.h` | 135 | 固定容量栈上向量 |

**必须先做**：批次 B 的 `VulkanTexture` 依赖本批次的 `VulkanLayout` / `GetVkFormat` / `TransitionLayout` / `VkFormatList`。

## 批次 B：纹理资源层（约 1700 行）

`VulkanMemory` / `VulkanTexture` / `VulkanSamplerCache` / `VulkanYcbcrConversionCache` / `VulkanStageImage` / `VulkanAttachment`。

## 已知的上游关键事实（探索阶段确认）

- `VulkanSwapChain.cpp:93/102` 用 `VulkanTexture` 的「包装已存在 `VkImage`」构造分支创建颜色与深度附件——**该分支是变更 5 的硬依赖**，必须完整可用
- `VulkanTexture` 有 4 个构造分支，其中「AHardwareBuffer / 外部图像」分支**按用户决策砍掉**（连 `VulkanStream` 内联类型、`mStream` 成员、`setExternalStream` 路径一并删除）；`Ycbcr` 结构**保留**（`VulkanYcbcrConversionCache` 需要），但其中的 externalFormat 分支删除
- `VulkanStageImage` 定义在上游 `VulkanStagePool.h` 第 131-190 行，既有移植时被砍；`ResourceType::StageImage` 枚举值已在 `Resource.h` 预留
- `VulkanSamplerCache` / `VulkanYcbcrConversionCache` 是 `tsl::robin_map` 使用者，两者均为**只增不删**缓存（无 `erase` 调用）

## 重复出现的教训（务必遵守）

变更 1 / 2 / 3 的实施中，**spec 里凭上游调用点推测出的 API 描述连续三次与上游实际定义不符**，每次都靠实施阶段逐行对照才发现。因此：

> **上游头文件是权威，spec 与 design 只是线索。发现不一致时以上游为准实现，并在报告里逐条列出。**

不要为了迎合 spec 而写出与上游不同的 API。
