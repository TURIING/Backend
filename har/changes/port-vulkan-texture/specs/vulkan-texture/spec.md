# Capability: vulkan-texture

## Purpose

Vulkan 纹理资源层：`VkImage` 的分配封装（`VulkanMemory`）、纹理资源对象与布局跟踪（`VulkanTexture`）、采样器缓存（`VulkanSamplerCache`）、YCbCr 转换缓存（`VulkanYcbcrConversionCache`）。上游对应约 1626 行。

本能力域是变更 5（交换链 / 渲染目标）的硬前置——`VulkanSwapChain` 用 `VulkanTexture` 的「包装已有 `VkImage`」构造分支创建颜色与深度附件，`VulkanAttachment` 的全部访问器都转发到它。

## ADDED Requirements
### Requirement: VulkanMemory 图像内存封装

`src/vulkan/VulkanMemory.{h,cpp}` SHALL 提供 `VkImage` 的 VMA 分配与释放封装：

- `AllocateImage(VkImageCreateInfo const&, VmaAllocationCreateInfo const&, VkImage*, VmaAllocation*)`
- 释放路径：`vmaDestroyImage` / `vmaFreeMemory`
- 遵循既有 `VulkanBuffer` / `VulkanBufferCache` 的 VMA 使用约定（`VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT`，后端单线程访问）

分工 SHALL 清晰：本能力域管 `VkImage`，既有 `VulkanBuffer` / `VulkanBufferCache` 管 `VkBuffer`，两者不重叠。

#### Scenario: 分配与释放往返

- **WHEN** 分配一个 `VkImage` 后立即释放
- **THEN** `vmaDestroyAllocator` 不触发 `Some allocations were not freed` 断言

#### Scenario: 不与缓冲路径重叠

- **WHEN** 检索 VMA 图像分配调用点
- **THEN** 只出现在本能力域与 `VulkanTexture` 中

### Requirement: VulkanTexture 资源对象

`src/vulkan/VulkanTexture.{h,cpp}` SHALL 定义 `VulkanTexture`（上游 1236 行），继承 `HwTexture` + `Resource`，提供三个构造分支：

| 分支 | 用途 | 要求 |
|---|---|---|
| 从零创建 | `createTextureR` 路径 | 按 `TextureFormat` / `TextureUsage` / levels / samples 创建 `VkImage` + `VkImageView` |
| 包装已有 `VkImage` | **交换链附件路径** | 接收 `VkImage` + `VkFormat` + `VkImageUsageFlags` + extent，创建 `VkImageView`，不分配内存 |
| 从 `VkImage` + 自定义 `VkImageView` | texture view / swizzle 路径 | 接收已创建的 `VkImageView` |

**SHALL NOT 提供**：AHardwareBuffer / 外部图像构造分支、`VulkanStream` 内联类型、`mStream` 成员、`setExternalStream` 相关路径。

功能面 SHALL 保留：布局跟踪（经 `NS_UTILS::RangeMap`）、`LoadImage`、`GenerateMipmaps`、`SetLinearTileMode`、`GetAttachment`。

`VulkanTexture` SHALL 提供 `VulkanAttachment` 所需的访问器：`GetImage()` / `GetFormat()` / `GetLayout()` / `GetImageView()` / `GetExtent2D()` / `GetSubresourceRange()` / `IsDepth()`。

#### Scenario: 包装已有 VkImage

- **WHEN** 以手工创建的 `VkImage` 构造 `VulkanTexture`（交换链附件场景）
- **THEN** 构造成功、不分配新内存，`GetImage()` 返回原句柄、`GetFormat()` / `GetExtent2D()` 返回传入值

#### Scenario: 从零创建

- **WHEN** 以 `TextureFormat::R8G8B8A8_UNORM` + `TextureUsage::SAMPLEABLE` 构造
- **THEN** `VkImage` 与 `VkImageView` 创建成功，初始布局为 `VK_IMAGE_LAYOUT_UNDEFINED`

#### Scenario: 布局跟踪

- **WHEN** 转换某 level 的布局后查询
- **THEN** 该 level 返回新布局，未涉及的 level 保持原布局

#### Scenario: 无外部图像依赖

- **WHEN** 检索 `VulkanTexture` 的构造签名与成员
- **THEN** 无 `AHardwareBuffer` / `ExternalImage` / `VulkanStream` 相关符号

### Requirement: VulkanAttachment 类型

`VulkanAttachment` SHALL 定义（落位见 tasks：`VulkanContext.h` 或 `VulkanTexture.h`），持有 `VulkanTexturePtr texture` / `uint8_t level` / `uint8_t layerCount` / `uint8_t layer`，提供：

- `IsDepth() const`
- `GetImage() const` / `GetImageView()` / `GetFormat() const` / `GetLayout() const` / `GetExtent2D() const` / `GetSubresourceRange() const`

全部访问器 SHALL 转发到 `m_texture`。上游该类型持 `fvkmemory::resource_ptr<VulkanTexture>`，本项目 SHALL 持 `VulkanTexturePtr`（`DECLARE_SHARE_PTR_CLASS` 别名）。

#### Scenario: 访问器转发

- **WHEN** 以某个 `VulkanTexture` 构造 `VulkanAttachment` 后调用 `GetFormat()`
- **THEN** 返回该 texture 的格式

#### Scenario: 使用 Ptr 别名

- **WHEN** 检索 `VulkanAttachment` 的成员声明
- **THEN** 类型为 `VulkanTexturePtr`，非裸 `NS_UTILS::SharedPtr<VulkanTexture>`

### Requirement: VulkanSamplerCache

`src/vulkan/VulkanSamplerCache.{h,cpp}` SHALL 提供 `VulkanSamplerCache`：

- 缓存键：`SamplerParams` + `VkSamplerYcbcrConversion` 句柄的组合
- `GetSampler(SamplerParams const&, VkSamplerYcbcrConversion conversion) → VkSampler`
- `Terminate()`：销毁全部缓存的 `VkSampler`

容器 SHALL 使用 `std::unordered_map`（上游为 `tsl::robin_map`，按 `backend-utils` 映射约定替换）。自定义 `Hash` 模板参数 SHALL 保留；若上游有自定义 `Equal`，同样保留。

本能力域的缓存为**只增不删**（采样器一旦创建即在池内复用），无 `erase` 调用，故 `robin_map` → `unordered_map` 的 `erase` 语义差异不适用。

#### Scenario: 同参数命中缓存

- **WHEN** 以相同 `SamplerParams` 两次调用 `GetSampler`
- **THEN** 两次返回同一 `VkSampler` 句柄

#### Scenario: 不同 YCbCr 转换区分

- **WHEN** 以相同 `SamplerParams` 但不同 `VkSamplerYcbcrConversion` 调用
- **THEN** 返回不同的 `VkSampler`

#### Scenario: Terminate 释放全部

- **WHEN** 创建 N 个采样器后调用 `Terminate()`
- **THEN** 全部 `VkSampler` 经 `vkDestroySampler` 释放，无泄漏

### Requirement: VulkanYcbcrConversionCache

`src/vulkan/VulkanYcbcrConversionCache.{h,cpp}` SHALL 提供 `VulkanYcbcrConversionCache`：

- 嵌套 `struct Params`：含 `VkSamplerYcbcrConversionCreateInfo conversion` + `VkFormat format` + `uint64_t externalFormat`
- `GetConversion(Params const&) → VkSamplerYcbcrConversion`
- `Terminate()`

容器 SHALL 使用 `std::unordered_map`（上游为 `tsl::robin_map`）。

依赖 `VulkanPlatform::ExternalYcbcrFormat`（变更 3 保留）。上游 `VulkanDriver.cpp` 的 `getYcbcrConversionParams(VulkanPlatform::ExternalYcbcrFormat const&)` 辅助函数属变更 7。

**用户决策记录**：本类型在「砍掉外部图像」的范围内本可一并砍掉，保留的理由是——148 行成本低，且保留后 `VulkanDriver` 中所有 `mYcbcrConversionCache` 调用点（含 `createProgramR` 的 pipeline prewarming 分支）可保持上游原样，无需改动上游实现。

#### Scenario: 同参数命中缓存

- **WHEN** 以相同 `Params` 两次调用 `GetConversion`
- **THEN** 返回同一 `VkSamplerYcbcrConversion`

#### Scenario: Terminate 释放全部

- **WHEN** 创建 N 个转换后调用 `Terminate()`
- **THEN** 全部经 `vkDestroySamplerYcbcrConversion` 释放

### Requirement: 适配约束
- `VulkanTexture.h` / `VulkanMemory.h` / `VulkanSamplerCache.h` / `VulkanYcbcrConversionCache.h` SHALL 位于 `src/vulkan/`
- 受 `SharedPtr` 管理的类型 SHALL 在类定义所在头文件声明 Ptr 别名（`DECLARE_SHARE_PTR_CLASS(VulkanTexture)`），其他位置一律用别名，禁止裸写 `NS_UTILS::SharedPtr<T>`
- 已声明 Ptr 别名的类型，函数参数 SHALL 用 `const Ptr&`，不按值传、不用 `const T&`（`.dsh/rules/cpp.md`）
- `VulkanTexture` SHALL 在 `Resource.h` 补 `GetTypeEnum<VulkanTexture>` 特化（返回 `ResourceType::Texture`），在 `ResourceManager::DestroyWithType` 补 `Texture` 销毁分支
- 上游文件头注释与 license 注释 SHALL 删除
- 注释 SHALL 遵循 `.dsh/rules/code-style.md`：不复述布局转换步骤（代码已自明）；对非平凡逻辑（如 `RangeMap` 的 level 范围跟踪、`LoadImage` 的布局屏障）须说明意图
- 类内声明 SHALL 遵循空行分组规则：访问修饰符段之间、特殊成员函数组与普通成员之间、函数声明区与成员变量区之间留空行

#### Scenario: 编译通过

- **WHEN** 构建整个 `Backend` 静态库
- **THEN** 本能力域的全部源文件编译通过，不依赖 Filament 任何头文件

#### Scenario: 命名与风格合规

- **WHEN** 检查本能力域新增的类型与函数
- **THEN** 命名遵循项目规范（`PascalCase` 类型与公有方法、`m_camelCase` 私有成员、`kPascalCase` 常量），头文件使用 `#pragma once`，不保留上游 license / 文件头注释
