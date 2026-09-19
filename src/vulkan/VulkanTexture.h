#pragma once

#include "Backend/DriverDefine.h"

#include "Utils/Hash.h"
#include "Utils/RangeMap.h"
#include "Utils/Utils.h"

#include <cstdint>
#include <unordered_map>

#include "HwDefine.h"
#include "vulkan/VkDef.h"
#include "vulkan/VulkanMemory.h"
#include "vulkan/resource/Resource.h"
#include "vulkan/stage/VulkanStagePool.h"
#include "vulkan/utils/Image.h"
#include "vulkan/commands/VulkanCommands.h"

BEGIN_NS_BACKEND

class PixelBufferDescriptor;
struct VulkanCommandBuffer;
struct VulkanTexture;
struct VulkanTextureState;

DECLARE_SHARE_PTR_CLASS(VulkanTexture);
DECLARE_SHARE_PTR_CLASS(VulkanTextureState);

// 纹理的共享状态：mip / swizzle 视图与源纹理共享同一份状态，图像视图缓存与布局跟踪因此只有一份
struct VulkanTextureState : public Resource {
    VulkanTextureState(const VulkanStagePoolPtr& stagePool, const VulkanCommandsPtr& commands, VmaAllocator allocator,
                       VkDevice device, VkImage image, VkDeviceMemory deviceMemory, VkFormat format, VkImageViewType viewType,
                       uint8_t levels, uint8_t layerCount, VkSamplerYcbcrConversion ycbcrConversion, VkImageUsageFlags usage,
                       bool isProtected);

    ~VulkanTextureState() override;

    VulkanTextureState(const VulkanTextureState&)            = delete;
    VulkanTextureState& operator=(const VulkanTextureState&) = delete;

    // 图像视图以「子资源范围 + 视图类型 + swizzle」为键缓存；键被整体按字节哈希，故不允许隐式填充
    struct ImageViewKey {
        VkImageSubresourceRange range;
        VkImageViewType         type;
        VkComponentMapping      swizzle;

        bool operator==(ImageViewKey const& rhs) const {
            auto const& lhs = *this;
            return lhs.range.aspectMask == rhs.range.aspectMask && lhs.range.baseMipLevel == rhs.range.baseMipLevel &&
                   lhs.range.levelCount == rhs.range.levelCount && lhs.range.baseArrayLayer == rhs.range.baseArrayLayer &&
                   lhs.range.layerCount == rhs.range.layerCount && lhs.type == rhs.type && lhs.swizzle.r == rhs.swizzle.r &&
                   lhs.swizzle.g == rhs.swizzle.g && lhs.swizzle.b == rhs.swizzle.b && lhs.swizzle.a == rhs.swizzle.a;
        }
    };
    static_assert(sizeof(ImageViewKey) == 40);

private:
    friend struct VulkanTexture;

    NODISCARD VkImageView getImageView(VkImageSubresourceRange range, VkImageViewType viewType, VkComponentMapping swizzle);

    void clearCachedImageViews() noexcept;

    VulkanStagePoolPtr m_stagePool;
    VulkanCommandsPtr const m_commands;
    VmaAllocator const    m_allocator;
    VkDevice const        m_device;

    VulkanTexturePtr m_sidecarMSAA;  // sidecar 由持有它的纹理一同持有

    VkImage const     m_textureImage;
    // 为 VK_NULL_HANDLE 表示图像由外部创建，本对象只借用不释放
    VkDeviceMemory const    m_textureImageMemory;
    VkFormat const          m_vkFormat;
    VkImageViewType const   m_viewType;
    VkImageSubresourceRange m_fullViewRange;

    // 外部图像可逐帧改写转换矩阵，故该字段非常量
    struct Ycbcr {
        VkSamplerYcbcrConversion conversion;

        bool operator==(Ycbcr const& other) const { return conversion == other.conversion; }

        bool operator!=(Ycbcr const& other) const { return !((*this) == other); }
    } m_ycbcr;

    VulkanLayout const      m_defaultLayout;
    VkImageUsageFlags const m_usage;
    bool const              m_isProtected;

    // 逐子资源稀疏跟踪布局，键为 (layer << 16) | level；无覆盖即表示 UNDEFINED
    NS_UTILS::RangeMap<uint32_t, VulkanLayout> m_subresourceLayouts;

    using ImageViewHash = NS_UTILS::hash::MurmurHashFn<ImageViewKey>;
    std::unordered_map<ImageViewKey, VkImageView, ImageViewHash> m_cachedImageViews;
};

// 纹理资源对象：持有 VkImage 与图像视图，并跟踪各子资源的当前布局
struct VulkanTexture : public HwTexture, public Resource {
    // 从零创建：驱动 createTextureR 的正路
    VulkanTexture(VkDevice device, VkPhysicalDevice physicalDevice, const VulkanContextPtr& context, VmaAllocator allocator,
                  const ResourceManagerPtr& resourceManager, const VulkanCommandsPtr& commands, SamplerType target, uint8_t levels,
                  TextureFormat tformat, uint8_t samples, uint32_t w, uint32_t h, uint32_t depth, TextureUsage tusage,
                  const VulkanStagePoolPtr& stagePool);

    // 包装已存在的 VkImage：交换链附件与内部创建的渲染目标；deviceMemory 为 VK_NULL_HANDLE 时图像不归本对象释放
    VulkanTexture(const VulkanContextPtr& context, VkDevice device, VmaAllocator allocator,
                  const ResourceManagerPtr& resourceManager, const VulkanCommandsPtr& commands, VkImage image, VkDeviceMemory deviceMemory,
                  VkFormat format, VkSamplerYcbcrConversion conversion, uint8_t samples, uint32_t width, uint32_t height,
                  uint32_t depth, TextureUsage tusage, const VulkanStagePoolPtr& stagePool);

    // 派生视图：只改主视图范围，图像、状态与视图缓存与源纹理共享
    VulkanTexture(VkDevice device, VkPhysicalDevice physicalDevice, const VulkanContextPtr& context, VmaAllocator allocator,
                  const VulkanCommandsPtr& commands, const VulkanTexturePtr& src, uint8_t baseLevel, uint8_t levelCount);

    VulkanTexture(VkDevice device, VkPhysicalDevice physicalDevice, const VulkanContextPtr& context, VmaAllocator allocator,
                  const VulkanCommandsPtr& commands, const VulkanTexturePtr& src, VkComponentMapping swizzle);

    ~VulkanTexture() override = default;

    void UpdateImage(const PixelBufferDescriptor& data, uint32_t width, uint32_t height, uint32_t depth, uint32_t xoffset,
                     uint32_t yoffset, uint32_t zoffset, uint32_t miplevel);

    NODISCARD VkImageViewType TransSamplerTypeToVkImageViewType() const { return m_state->m_viewType; }

    NODISCARD VkImageSubresourceRange const& GetPrimaryViewRange() const { return m_primaryViewRange; }

    // 采样该纹理时应处于的布局
    NODISCARD VulkanLayout GetSamplerLayout() const;

    // 该纹理按其用途「应该」处于的布局，与当前实际布局无关
    NODISCARD VulkanLayout GetDefaultLayout() const { return m_state->m_defaultLayout; }

    // 以恒等 swizzle 取（或创建）可作渲染目标附件的图像视图
    NODISCARD VkImageView GetAttachmentView(const VkImageSubresourceRange& range, VkImageViewType type);

    NODISCARD VkImageView GetView(const VkImageSubresourceRange& range);

    NODISCARD VkImage GetImage() const { return m_state->m_textureImage; }

    NODISCARD VkFormat GetFormat() const { return m_state->m_vkFormat; }

    // 基准层级的尺寸；按 level 折算的尺寸见 VulkanAttachment::GetExtent2D
    NODISCARD VkExtent2D GetExtent2D() const { return { width, height }; }

    NODISCARD VulkanLayout GetLayout(uint32_t layer, uint32_t level) const;

    NODISCARD VkImageAspectFlags TransVkFormatToVkImageAspectFlags() const;

    NODISCARD bool IsTransientAttachment() const { return (m_state->m_usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) != 0; }

    NODISCARD bool IsSampleable() const { return (m_state->m_usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0; }

    NODISCARD bool GetIsProtected() const { return m_state->m_isProtected; }

    void SetSidecar(const VulkanTexturePtr& sidecar) { m_state->m_sidecarMSAA = sidecar; }
    NODISCARD VulkanTexturePtr GetSidecar() const { return m_state->m_sidecarMSAA; }

    // 返回是否真的录入了转换命令——新旧布局等价时无屏障，返回 false；调用方可安全忽略
    bool TransitionLayout(VulkanCommandBuffer* commands, const VkImageSubresourceRange& range, VulkanLayout newLayout);
    bool TransitionLayout(VkCommandBuffer cmdbuf, const VkImageSubresourceRange& range, VulkanLayout newLayout);

    void AttachmentToSamplerBarrier(VulkanCommandBuffer* commands, const VkImageSubresourceRange& range);

    void SamplerToAttachmentBarrier(VulkanCommandBuffer* commands, const VkImageSubresourceRange& range);

    // 隐式布局变化（如 render pass 结束时由驱动改写）用，不经屏障
    void SetLayout(const VkImageSubresourceRange& range, VulkanLayout newLayout);

    // 外部图像可能逐帧改写转换，改写后已缓存的图像视图失效
    void SetYcbcrConversion(VkSamplerYcbcrConversion conversion);

private:
    NODISCARD VkImageView getImageView(VkImageSubresourceRange range, VkImageViewType viewType, VkComponentMapping swizzle);

    void updateImageWithBlit(const PixelBufferDescriptor& hostData, uint32_t width, uint32_t height, uint32_t depth,
                             uint32_t miplevel);

    VulkanTextureStatePtr m_state;

    VkImageSubresourceRange m_primaryViewRange;  // 主视图范围：绑定到采样器的那个图像视图所覆盖的子资源范围

    VkComponentMapping m_swizzle{};
};

// 渲染目标附件：某个纹理上 (layer, level) 子资源的视图，全部访问器转发给纹理
struct VulkanAttachment {
    VulkanTexturePtr texture;
    uint8_t          level      = 0;
    uint8_t          layerCount = 1;
    uint8_t          layer      = 0;

    NODISCARD bool IsDepth() const;

    NODISCARD VkImage GetImage() const;

    NODISCARD VkFormat GetFormat() const;

    NODISCARD VulkanLayout GetLayout() const;

    NODISCARD VkExtent2D GetExtent2D() const;

    NODISCARD VkImageView GetImageView();

    NODISCARD VkImageSubresourceRange GetSubresourceRange() const;
};

END_NS_BACKEND
