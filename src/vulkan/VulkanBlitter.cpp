#include "vulkan/VulkanBlitter.h"

#include "vulkan/VulkanContext.h"
#include "vulkan/VulkanTexture.h"
#include "vulkan/commands/VulkanCommandBuffer.h"
#include "vulkan/commands/VulkanCommands.h"
#include "vulkan/utils/Image.h"

#include "Utils/Log.h"
#include "Utils/Macro.h"

BEGIN_NS_BACKEND

namespace {

void BlitFast(VulkanCommandBuffer* commands, VkImageAspectFlags aspect, VkFilter filter, VulkanAttachment src, VulkanAttachment dst,
              VkOffset3D const* srcRect, VkOffset3D const* dstRect) {
    VkCommandBuffer const cmdBuffer = commands->Buffer();
    if constexpr (BVK_ENABLED(BVK_DEBUG_BLITTER)) {
        LOG_DEBUG(
            "Fast blit from image={} level={} layer={} layout={} src-rect=({},{},{})->({},{},{})"
            " to image={} level={} layer={} layout={} dst-rect=({},{},{})->({},{},{})",
            static_cast<void const*>(src.texture->GetImage()), static_cast<int>(src.level), static_cast<int>(src.layer),
            src.GetLayout(), srcRect[0].x, srcRect[0].y, srcRect[0].z, srcRect[1].x, srcRect[1].y, srcRect[1].z,
            static_cast<void const*>(dst.texture->GetImage()), static_cast<int>(dst.level), static_cast<int>(dst.layer),
            dst.GetLayout(), dstRect[0].x, dstRect[0].y, dstRect[0].z, dstRect[1].x, dstRect[1].y, dstRect[1].z);
    }

    VkImageSubresourceRange const srcRange = src.GetSubresourceRange();
    VkImageSubresourceRange const dstRange = dst.GetSubresourceRange();

    VulkanLayout oldSrcLayout = src.GetLayout();
    VulkanLayout oldDstLayout = dst.GetLayout();

    src.texture->TransitionLayout(commands, srcRange, VulkanLayout::TRANSFER_SRC);
    dst.texture->TransitionLayout(commands, dstRange, VulkanLayout::TRANSFER_DST);

    VkImageBlit const blitRegions[1] = { {
        .srcSubresource = { aspect, src.level, src.layer, 1 },
        .srcOffsets     = { srcRect[0], srcRect[1] },
        .dstSubresource = { aspect, dst.level, dst.layer, 1 },
        .dstOffsets     = { dstRect[0], dstRect[1] },
    } };
    vkCmdBlitImage(cmdBuffer, src.GetImage(), VK_UTILS::GetVkLayout(VulkanLayout::TRANSFER_SRC), dst.GetImage(),
                   VK_UTILS::GetVkLayout(VulkanLayout::TRANSFER_DST), 1, blitRegions, filter);

    // 拷贝结束后把两侧恢复到各自「按用途应有的」布局，调用方无须感知中间态
    if (oldSrcLayout == VulkanLayout::UNDEFINED) {
        oldSrcLayout = src.texture->GetDefaultLayout();
    }
    if (oldDstLayout == VulkanLayout::UNDEFINED) {
        oldDstLayout = dst.texture->GetDefaultLayout();
    }

    src.texture->TransitionLayout(commands, srcRange, oldSrcLayout);
    dst.texture->TransitionLayout(commands, dstRange, oldDstLayout);
}

void ResolveFast(VulkanCommandBuffer* commands, VkImageAspectFlags aspect, VulkanAttachment src, VulkanAttachment dst) {
    VkCommandBuffer const cmdBuffer = commands->Buffer();
    if constexpr (BVK_ENABLED(BVK_DEBUG_BLITTER)) {
        LOG_DEBUG("Fast resolve from image={} level={} layout={} to image={} level={} layout={}",
                  static_cast<void const*>(src.texture->GetImage()), static_cast<int>(src.level), src.GetLayout(),
                  static_cast<void const*>(dst.texture->GetImage()), static_cast<int>(dst.level), dst.GetLayout());
    }

    VkImageSubresourceRange const srcRange = src.GetSubresourceRange();
    VkImageSubresourceRange const dstRange = dst.GetSubresourceRange();

    VulkanLayout oldSrcLayout = src.GetLayout();
    VulkanLayout oldDstLayout = dst.GetLayout();

    dst.texture->TransitionLayout(commands, dstRange, VulkanLayout::TRANSFER_DST);

    LOG_ASSERT(aspect != VK_IMAGE_ASPECT_DEPTH_BIT);
    VkExtent2D const       extent         = src.GetExtent2D();
    VkImageResolve const   resolveRegions[1] = { {
        .srcSubresource = { aspect, src.level, src.layer, 1 },
        .srcOffset      = { 0, 0, 0 },
        .dstSubresource = { aspect, dst.level, dst.layer, 1 },
        .dstOffset      = { 0, 0, 0 },
        .extent         = { extent.width, extent.height, 1 },
    } };
    vkCmdResolveImage(cmdBuffer, src.GetImage(), VK_UTILS::GetVkLayout(src.GetLayout()), dst.GetImage(),
                      VK_UTILS::GetVkLayout(VulkanLayout::TRANSFER_DST), 1, resolveRegions);

    if (oldSrcLayout == VulkanLayout::UNDEFINED) {
        oldSrcLayout = src.texture->GetDefaultLayout();
    }
    if (oldDstLayout == VulkanLayout::UNDEFINED) {
        oldDstLayout = dst.texture->GetDefaultLayout();
    }
    src.texture->TransitionLayout(commands, srcRange, oldSrcLayout);
    dst.texture->TransitionLayout(commands, dstRange, oldDstLayout);
}

}  // namespace

VulkanBlitter::VulkanBlitter(VkPhysicalDevice physicalDevice, const VulkanCommandsPtr& commands) noexcept
    : m_physicalDevice(physicalDevice), m_commands(commands) {}

void VulkanBlitter::Resolve(VulkanAttachment dst, VulkanAttachment src) {
    // 源与目标的 aspect 必须一致
    VkImageAspectFlags const aspect = src.texture->GetImageAspect();

    LOG_ASSERT(!(aspect & VK_IMAGE_ASPECT_DEPTH_BIT));

    if constexpr (BVK_ENABLED(BVK_DEBUG_BLIT_FORMAT)) {
        VkPhysicalDevice const gpu = m_physicalDevice;
        VkFormatProperties     info;
        vkGetPhysicalDeviceFormatProperties(gpu, src.GetFormat(), &info);
        if (!(info.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
            LOG_ERROR("Source format is not blittable {}", static_cast<int>(src.GetFormat()));
            return;
        }
        vkGetPhysicalDeviceFormatProperties(gpu, dst.GetFormat(), &info);
        if (!(info.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
            LOG_ERROR("Destination format is not blittable {}", static_cast<int>(dst.GetFormat()));
            return;
        }
    }

    // 受保护内容须走受保护的命令缓冲；本项目尚未建立受保护池，统一用普通命令缓冲
    VulkanCommandBuffer& commands = m_commands->Get();
    commands.Acquire(src.texture);
    commands.Acquire(dst.texture);
    ResolveFast(&commands, aspect, src, dst);
}

void VulkanBlitter::Blit(VkFilter filter, VulkanAttachment dst, VkOffset3D const* dstRectPair, VulkanAttachment src,
                         VkOffset3D const* srcRectPair) {
    if constexpr (BVK_ENABLED(BVK_DEBUG_BLIT_FORMAT)) {
        VkPhysicalDevice const gpu = m_physicalDevice;
        VkFormatProperties     info;
        vkGetPhysicalDeviceFormatProperties(gpu, src.GetFormat(), &info);
        if (!(info.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
            LOG_ERROR("Source format is not blittable {}", static_cast<int>(src.GetFormat()));
            return;
        }
        vkGetPhysicalDeviceFormatProperties(gpu, dst.GetFormat(), &info);
        if (!(info.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
            LOG_ERROR("Destination format is not blittable {}", static_cast<int>(dst.GetFormat()));
            return;
        }
    }

    // 源与目标的 aspect 必须一致
    VkImageAspectFlags const aspect = src.texture->GetImageAspect();
    VulkanCommandBuffer&     commands = m_commands->Get();
    commands.Acquire(src.texture);
    commands.Acquire(dst.texture);
    BlitFast(&commands, aspect, filter, src, dst, srcRectPair, dstRectPair);
}

void VulkanBlitter::Terminate() noexcept {}

END_NS_BACKEND
