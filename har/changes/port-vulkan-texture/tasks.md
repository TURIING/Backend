# Tasks: port-vulkan-texture

## 1. utils 工具层：Definitions 与 StaticVector

- [x] 1.1 `src/vulkan/utils/Definitions.h`：`VkFormatList` → `std::vector<VkFormat>`；机械复制上游 `ALL_VK_FORMATS` 数组（约 250 项）+ 编译期长度断言
- [x] 1.2 `Definitions.h`：`DescriptorSetMask` / `UniformBufferBitmask` / `SamplerBitmask` 基于 `NS_UTILS::Bitset32` 实现（含 `operator[]` / `Set` / `Unset` / `ForEachSetBit`）
- [x] 1.3 `src/vulkan/utils/StaticVector.h`：`VK_UTILS::StaticVector<T, N>`（`PushBack` / `PopBack` / `Size` / `Empty` / `Clear` / `operator[]` / 迭代器），溢出 debug 断言
- [x] 1.4 复核：`StaticVector` 未被 `std::vector` 替代；既有 `VulkanCommandBuffer` 的 `std::array<T, 2>` 保持不动

## 2. utils 工具层：Image 与 Conversion

- [x] 2.1 `src/vulkan/utils/Image.h` / `.cpp`：`VulkanLayout` 枚举（逐值对齐上游）+ `TransitionLayout` + `ReduceSampleCount` + 格式 aspect 辅助
- [x] 2.2 `src/vulkan/utils/Conversion.h` / `.cpp`：15 个映射函数，**全部单值映射用 `CASE_FROM_TO`**
- [x] 2.3 **合并 `IsVkDepthFormat`**：既有 `src/vulkan/VkUtils.h` 已有一份，删除上游重复定义，保留既有实现
- [x] 2.4 `GetVkFormat`：为 `TextureFormat` 每个枚举值提供分支；若上游有 `default` 兜底，保留但加 `LOG_ASSERT(false)`
- [x] 2.5 为 `Conversion.cpp` 单独开启 `-Wswitch`，确认无「枚举值未处理」告警
- [x] 2.6 复核：`VulkanLayout` 枚举值与上游逐值比对

## 3. utils 工具层：Spirv 与 Helper

- [x] 3.1 `src/vulkan/utils/Spirv.h` / `.cpp`：SPIR-V 魔数/版本/长度校验 + `VkShaderModule` 创建辅助；**不引入** `glslang` / `spirv-tools`
- [x] 3.2 `src/vulkan/utils/Helper.h`：头文件级小工具（Hash 特化、判等辅助），逐符号对齐上游
- [x] 3.3 确认六个文件均使用 `VK_UTILS` 命名空间、无 `fvkutils`、无 `using namespace bluevk;`

## 4. VulkanMemory

- [x] 4.1 `src/vulkan/VulkanMemory.h` / `.cpp`：`VkImage` 的 VMA 分配封装（`AllocateImage` + 释放路径）
- [x] 4.2 复核：`VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT` 约定与既有 `VulkanBuffer` 一致
- [x] 4.3 复核：VMA 图像分配点只出现在本层与 `VulkanTexture`，不与缓冲路径重叠

## 5. VulkanTexture

- [x] 5.1 `src/vulkan/VulkanTexture.h`：类声明 + 3 个构造分支（从零 / 包装已有 `VkImage` / 已有 `VkImageView`）+ 访问器（`GetImage` / `GetFormat` / `GetLayout` / `GetImageView` / `GetExtent2D` / `GetSubresourceRange` / `IsDepth`）
- [x] 5.2 `VulkanTexture.h`：**删除** `VulkanStream` 内联类型、`mStream` 成员、AHardwareBuffer / 外部图像构造分支；**保留** `Ycbcr` 结构中的 `VkSamplerYcbcrConversion conversion` 字段
- [x] 5.3 `VulkanTexture.h`：布局跟踪成员（`NS_UTILS::RangeMap`）+ Ptr 别名（`DECLARE_SHARE_PTR_CLASS(VulkanTexture)`）
- [x] 5.4 `src/vulkan/VulkanTexture.cpp`：3 个构造分支实现 + `LoadImage` + `GenerateMipmaps` + `SetLinearTileMode` + 布局跟踪方法
- [x] 5.5 复核：构造分支 2（包装已有 `VkImage`）逐字段对照 `VulkanSwapChain.cpp:93/102` 的调用点
- [x] 5.6 复核：全部 Ptr 类型参数用 `const Ptr&`，无裸 `NS_UTILS::SharedPtr<T>` 出现在函数签名

## 6. VulkanAttachment

- [x] 6.1 确定 `VulkanAttachment` 落位（`VulkanContext.h` 或 `VulkanTexture.h`），持 `VulkanTexturePtr` + `level` / `layerCount` / `layer`
- [x] 6.2 实现 7 个访问器（全部转发到 texture）+ `IsDepth()`
- [x] 6.3 复核：访问器集合覆盖变更 5 的 `VulkanRenderTarget` / `VulkanFboCache` 全部读取点

## 7. 采样器与 YCbCr 缓存

- [x] 7.1 `src/vulkan/VulkanSamplerCache.h` / `.cpp`：`std::unordered_map` 替换 `tsl::robin_map`，**保留自定义 Hash 模板参数**；`GetSampler` + `Terminate`
- [x] 7.2 `src/vulkan/VulkanYcbcrConversionCache.h` / `.cpp`：同上；`Params` 嵌套结构 + `GetConversion` + `Terminate`
- [x] 7.3 记录：本变更两个使用者均为只增不删缓存，无 `erase` 调用，`robin_map` → `unordered_map` 的 `erase` 语义差异不适用（对应 design D6）
- [x] 7.4 复核：`SamplerParams` 作为哈希键时，其位域布局已在变更 2 对齐上游

## 8. VulkanStageImage 回补

- [x] 8.1 `src/vulkan/stage/VulkanStagePool.h`：新增 `class VulkanStageImage` + 嵌套 `class Resource`（回收回调）
- [x] 8.2 `VulkanStagePool.h` / `.cpp`：新增 `AcquireStageImage`，按三元组复用；`Gc()` 淘汰与 `Terminate()` 释放同时覆盖缓冲与图像
- [x] 8.3 `src/vulkan/resource/Resource.h`：`class VulkanStageImage;` 前向声明 + `GetTypeEnum<VulkanStageImage>` 特化声明
- [x] 8.4 `src/vulkan/resource/Resource.cpp`：特化定义返回 `ResourceType::StageImage`
- [x] 8.5 `src/vulkan/resource/ResourceManager.cpp`：`DestroyWithType` 补 `StageImage` 分支
- [x] 8.6 `src/vulkan/resource/Resource.h` / `Resource.cpp`：补 `GetTypeEnum<VulkanTexture>` 特化（返回 `ResourceType::Texture`）
- [x] 8.7 `ResourceManager.cpp`：`DestroyWithType` 补 `Texture` 分支
- [x] 8.8 复核：`VulkanStageImage` 的前向声明用 `class` 关键字（与定义一致）

## 9. 验证

- [x] 9.1 全量构建（`cmake --build build`）无错误、无新增警告
- [x] 9.2 运行 `bin/BackendTests`：8 帧往返正常、无 `LOG_CRITICAL`、exit 0（既有缓冲/暂存路径零回归）
- [x] 9.3 `Conversion.cpp` 启用 `-Wswitch` 编译，确认 `TextureFormat` 穷举无遗漏
- [x] 9.4 单元测试：`VulkanStageImage` 同尺寸二次获取命中复用、不同尺寸不复用、归还后可复用
- [x] 9.5 单元测试：`VulkanTexture` 以「包装手工创建的 `VkImage`」构造后，`GetImage` / `GetFormat` / `GetExtent2D` 返回正确值
- [x] 9.6 单元测试：`VulkanSamplerCache` 同参数命中、不同 YCbCr 转换区分、`Terminate` 后无泄漏
- [x] 9.7 断言 `Terminate()` 路径 `vmaDestroyAllocator` 不触发 `Some allocations were not freed`
- [x] 9.8 **记录局限**：`VulkanTexture::UpdateImage` / `updateImageWithBlit` 需要 `VulkanCommands` 录制命令，而 `VulkanDriver` 尚未持有 `mCommands`（属变更 7）；这些路径本变更**不验证**，只以编译通过 + 与上游逐行比对为准
- [x] 9.9 **记录局限**：`GetVkFormat` 的映射正确性只由「枚举穷举编译检查 + 抽样单元测试」覆盖，未逐值运行时验证
- [x] 9.10 记录 `VulkanAttachment` 的最终落位与理由，回填至 design 的 Open Questions

## 实施记录 · 批次 A（`src/vulkan/utils/`）

### 与 spec / design 不一致的上游实际定义（一律以上游为准）

| # | spec 说 | 上游实际 |
|---|---|---|
| 1 | `VulkanLayout` 值为 `COLOR_ATTACHMENT` / `DEPTH_STENCIL_ATTACHMENT` / `DEPTH_SAMPLER` / `SHADER_READABLE` / `SHADER_WRITABLE` / `TRANSFER_SRC` / `TRANSFER_DST` / `PRESENT` | 11 个值：`UNDEFINED` / `STAGING` / `FRAG_READ` / `VERT_READ` / `TRANSFER_SRC` / `TRANSFER_DST` / `DEPTH_STENCIL_ATTACHMENT` / `DEPTH_SAMPLER` / `PRESENT` / `COLOR_ATTACHMENT` / `COLOR_ATTACHMENT_RESOLVE`。**无** `SHADER_READABLE` / `SHADER_WRITABLE` |
| 2 | `TransitionLayout` 在 `Conversion`，6 个散参数 | 在 `Image.{h,cpp}`，签名 `bool TransitionLayout(VkCommandBuffer, VulkanLayoutTransition)`——参数是**结构体** |
| 3 | `ReduceSampleCount(uint8_t requested)` | `ReduceSampleCount(uint8_t sampleCount, VkSampleCountFlags mask)`——由调用方传设备能力掩码 |
| 4 | `Spirv` 做「魔数/版本/长度校验 + `VkShaderModule` 创建」 | 只有一个 `WorkaroundSpecConstant`：把 `OpSpecConstant*` 改写成 `OpConstant`，绕开部分驱动的 spec constant 缺陷。**无校验、无 shader module 创建** |
| 5 | `Helper.h` 提供 `Hash` 特化 | 提供 `enumerate` 模板 + `EXPAND_ENUM` 宏 + `equivalent`。**无 Hash 特化**（`enumerate` 因与本项目 `VkUtils.h` 重名而未移植，只保留 `equivalent`） |
| 6 | `DescriptorSetMask` 基于 `Bitset32` | 上游是 `bitset8`；`UniformBufferBitmask` / `SamplerBitmask` 才是 `bitset64` |
| 7 | `StaticVector` 有 `Empty()` | 上游**没有** `empty()`（为满足 spec 已补上，属新增） |
| 8 | `IsVkFormatDepthOrStencil` 辅助函数 | 上游无此函数；能力由 `IsVkDepthFormat` / `IsVkStencilFormat` 提供 |
| 9 | `GetVkFormat` 不留 `default` 兜底 | 上游 `TextureFormat` 版本 default 静默返回 `VK_FORMAT_UNDEFINED`；已给 `UNUSED` 补显式分支使 default 事实不可达 |
| 10 | `Definitions.h` 需提供 `ALL_VK_FORMATS` | `VkDef.h` 已有一份（变更 2 产出）。采取「迁移到 `Definitions.h` + `VkDef.h` 转发别名」而非两份并存 |

### 移植中自行发现并修复的缺陷

1. `GetVkFormat(PixelDataFormat, PixelDataType)` 的三个类型短路判断误置于 `switch` 之后 → `(RGBA, USHORT_565)` 会落进 `RGBA` 分支返回错误格式。已上移，与上游一致。
2. `GetBytesPerPixel` 误把 `RGB9_E5` 归入 2 字节（其 32bpp，仅因历史原因排在 16 位段）→ 已改为 4。

### 验证

- `-Wswitch` 单独编译 `Conversion.cpp`：0 告警；**负向对照**（删一行 `CASE_FROM_TO` 后立即报 `-Wswitch`）证明该检查有效
- 与上游逐值差分：`GetVkFormat`(109) / `GetTexelBlockSize`(247) / `GetComponentCount`(96) / `GetComponentType`(95) / `GetVkFormatLinear`(33) 等 19 个映射的 case 集全等
- 14 条运行期断言（含 `GetBytesPerPixel(RGB9_E5) == 4`）
- `IsVkDepthFormat` / `IsVkStencilFormat` 各只有一处定义（`VkUtils.h`），上游重复定义已删除

### 待批次 B / 变更 5 兑现

| 项 | 说明 |
|---|---|
| `VulkanLayout` 的真实值集 | 变更 5 的 `VulkanRenderTarget` 屏障发射用它，届时首次真实消费 |
| `StaticVector::Back()` 的下标偏一写法 | 保留上游语义（`*(begin() + mSize)`），已加注释；变更 6 消费时须留意 |
| `DescriptorSetMask` 是 `bitset8` | 变更 6 的 `VulkanDescriptorSetCache::Commit` 用它 |

## 实施记录 · 批次 B（纹理资源层）

### 与 spec / design 不一致的上游实际定义（一律以上游为准）

| # | spec / design 说 | 上游实际 |
|---|---|---|
| 1 | `VulkanMemory.{h,cpp}` 是「`VkImage` 的 VMA 分配封装」，含 `AllocateImage(...)` | 上游 `VulkanMemory.h` 只定义 `VulkanBufferBinding` + `VulkanGpuBuffer`；`.cpp` 是 VMA 实现编译单元（`#define VMA_IMPLEMENTATION`）。**上游纹理根本不用 VMA**——`VulkanTexture` 用 `vkCreateImage`/`vkAllocateMemory`/`vkBindImageMemory`。本项目采取「把既有 `VulkanGpuBuffer` 迁到 `VulkanMemory.h`，`VmaImpl.cpp` 更名 `VulkanMemory.cpp`」，与上游文件布局 1:1 |
| 2 | `VulkanStageImage` 是唯一新增的 stage 资源类型，`GetTypeEnum<VulkanStageImage>` | 上游特化的是 `getTypeEnum<VulkanStageImage::Resource>()`（进 ResourceManager 的是包装类，不是池对象；与既有 `VulkanStageBuffer::Segment` 同构）。特化声明放 `VulkanStagePool.h`、定义放 `VulkanStagePool.cpp`，前向声明关键字问题不存在 |
| 3 | `AcquireStageImage(VkFormat, uint32_t, uint32_t)` | 上游为 `acquireImage(PixelDataFormat format, PixelDataType type, uint32_t width, uint32_t height)`——由池内部调 `getVkFormat(format, type)` 换算，调用方给的是主机侧格式 |
| 4 | `VulkanTexture::LoadImage` / `GenerateMipmaps` / `SetLinearTileMode` / `GetAttachment` | 上游对应 `updateImage` / `updateImageWithBlit`（mipmap 生成在 `VulkanBlitter`）/ `getAttachmentView`；`getExtent2D` 只在 `VulkanAttachment` 上，纹理侧需要经 `texture->width >> level` 自行折算 |
| 5 | 构造分支 4 是「`VkImage` + 自定义 `VkImageView`」 | 上游第 3、4 个构造分支都是以既有 `VulkanTexture` 为源派生视图（mip 范围 / swizzle），**没有**「传 `VkImage` + 自定义 `VkImageView`」的分支；两个视图构造与源共享 `VulkanTextureState` |
| 6 | `VulkanTexture` 直接持有布局跟踪与视图缓存 | 上游把这些成员放在**独立资源类型 `VulkanTextureState`**（`TEXTURE_STATE = 7`，本项目枚举已预留），视图构造共享它；因此本项目多补一条 `GetTypeEnum<VulkanTextureState>` + `DestroyWithType` 分支 |
| 7 | `VulkanAttachment` 的访问器不含 `GetPrimaryViewRange` 之类 | 上游 `VulkanAttachment` 7 个访问器与 spec 一致；但 `getImageView()` 在 `layerCount > 1` 时必须走 `VK_IMAGE_VIEW_TYPE_2D_ARRAY`，且 `getExtent2D()` 是 `width >> level` 折算，不是纹理基准尺寸 |
| 8 | `VulkanYcbcrConversionCache::Params` 含 `VkSamplerYcbcrConversionCreateInfo conversion` | 上游 `conversion` 字段是 `fvkutils::SamplerYcbcrConversion`（4 字节位域结构，本项目在 `Definitions.h`），加 `VkFormat` + `uint64_t externalFormat` 恰为 16 字节 |
| 9 | `VulkanSamplerCache` 缓存键是「SamplerParams + VkSamplerYcbcrConversion 组合」 | 上游键是 `Params{ SamplerParams, uint32_t padding, VkSamplerYcbcrConversion }`（16 字节，`padding` 为对齐而存在），等值判定用 `SamplerParams::EqualTo` |
| 10 | design D5：`DestroyWithType` 分支数 7 → 8 | 实际 7 → **10**（`StageImage` + `Texture` + `TextureState`）；变更 5 的「8 → 12」需相应修正 |
| 11 | `HwTexture` 已存在 | 本项目 `HwDefine.h` 缺 `HwTexture`，本批次补齐（去掉上游的 `HwStream* hwStream`——视频流按既定决策砍掉） |
| 12 | 无需改既有类型层 | 上游 `getUsage` 用 `~TextureUsage::ALL_ATTACHMENTS` 判「用途只含附件标志」，本项目 `TextureUsage` 缺 `operator~`，在 `DriverDefine.h` 补一个（与既有 `operator|` / `operator&` 同组） |
| 13 | `VulkanLayout` 可直接裸写 | 本项目把它放在 `VK_UTILS`（批次 A 决策），上游是置于后端命名空间；在 `VkDef.h` 补 `using VK_UTILS::VulkanLayout;`（与既有 `VkFormatList` 别名同组）以保持上游调用点零改写 |
| 14 | `VulkanStagePool` 可自行取命令缓冲 | 上游池持有 `VulkanCommands*`，本项目驱动侧尚未持有（属变更 7），构造函数加第 4 个参数，为空时跳过图像布局转换 |

### 移植中自行发现并处理的缺陷 / 取舍

1. `updateImage` 的 3 分量→4 分量 reshape 依赖 `DataReshaper`，而该前端件在变更 1 的 proposal 中明确「不移植」→ 本项目按原样上传，已在代码内标注该差异（不引入第二处数据整形实现）。
2. 上游 `VulkanTextureState` 的 `mSoftwareYUVStaging`（`VkDeviceMemory` + `VkBuffer` + `Platform::ExternalImageHandle`）随外部图像一起删除。
3. 上游 `VulkanTexture.h` 里 `getExtent2D()` 只挂在 `VulkanAttachment`；本项目在纹理上另行提供 `GetExtent2D()`（基准层级尺寸）以满足验收用例，属**新增**而非上游 API。
4. `TransitionLayout` 的返回值是「是否真的录入了屏障」，上游有大量忽略返回值的调用点，故本项目**不加** `NODISCARD`（加了会引入 4 处 `-Wunused-result` 告警）。

### 验证

- 全量构建 0 error / 0 warning（`-Wnonportable-include-path` 除外）
- `bin/UtilsTests`：154 tests passed，exit 0
- `bin/BackendTests`：8 帧往返正常，exit 0
- 运行期验证（`tests/App.cpp::VerifyTextureLayer`，用平台句柄自建 VMA 分配器 / `ResourceManager` / 默认 `VulkanContext`）：
  - 暂存图像：同尺寸复用命中、不同尺寸不复用、归还后可复用 —— 三条均通过
  - 纹理：以手工 `VkImage` 走「包装已有 `VkImage`」分支，`GetImage` / `GetFormat` / `GetExtent2D` 正确；`SetLayout` 后 level 0 变、level 1 保持 `UNDEFINED`；`VulkanAttachment` 转发正确
  - 采样器缓存：同参数两次返回同一句柄；MoltenVK 上 `vkCreateSamplerYcbcrConversion` 实际成功，不同转换返回不同句柄
  - `stagePool->Terminate()` + `resourceManager->Terminate()` 后 `vmaCalculateStatistics` 的 `allocationCount == 0`，`vmaDestroyAllocator` 不触发泄漏断言
- **负向对照**（证明检查有效，跑完即还原）：
  1. 关掉暂存图像的复用查找 → `reuseHit=false` / `recycleHit=false`，用例失败
  2. 让 `Terminate()` 不释放图像 → 日志「2 VMA allocations are still alive」且 `vmaDestroyAllocator` 命中 VMA 的 `Some allocations were not freed before destruction of this memory block!` 断言

### 待变更 5 / 7 兑现

| 项 | 说明 |
|---|---|
| `VulkanTexture` 的 `updateImage` / `updateImageWithBlit` | 需要 `VulkanCommands`，变更 7 接线后才会首次真实消费；届时 `VulkanStagePool` 的第 4 个构造实参也要从 `nullptr` 换成 `mCommands` |
| `VulkanTextureState` 是否真需要独立资源类型 | 变更 5/7 若发现视图构造不被使用，可回收为纹理内联成员（届时同步去掉 `TextureState` 的 `DestroyWithType` 分支） |
| `VulkanTexture` 视图构造（mip / swizzle） | 本批次无调用方，未运行时验证；变更 7 的 `createTextureView*` 首次消费 |
| `VulkanYcbcrConversionCache` | 本批次无调用方（`getYcbcrConversionParams` 属变更 7），未运行时验证 |

