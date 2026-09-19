#pragma once

#include "Backend/DriverDefine.h"
#include "Backend/Namespace.h"

#include "Utils/Debug.h"
#include "Utils/Log.h"
#include "Utils/Macro.h"

#include <cstdint>
#include <volk.h>

BEGIN_NS_BACKEND

namespace VK_UTILS {

// 图像在命令流中的布局状态；被 VulkanTexture 用于布局跟踪、被 VulkanFboCache 用于渲染通道
enum class VulkanLayout : uint8_t {
    UNDEFINED,                 // VkImage 刚创建、尚未发生任何转换时的状态
    STAGING,                   // 片元/顶点着色器均可读写
    FRAG_READ,                 // 仅片元着色器可读
    VERT_READ,                 // 仅顶点着色器可读
    TRANSFER_SRC,              // 作为拷贝操作的源
    TRANSFER_DST,              // 作为拷贝操作的目标
    DEPTH_STENCIL_ATTACHMENT,  // 作为深度/模板附件
    DEPTH_SAMPLER,             // 既作深度附件又作采样源
    PRESENT,                   // 交换链图像，将被呈现
    COLOR_ATTACHMENT,          // 颜色附件，同时也可被采样
    COLOR_ATTACHMENT_RESOLVE,  // 颜色附件的 MSAA 解析目标
};

struct VulkanLayoutTransition {
    VkImage                 image;
    VulkanLayout            oldLayout;
    VulkanLayout            newLayout;
    VkImageSubresourceRange subresources;
};

NODISCARD inline const char *TransVulkanLayoutToString(VulkanLayout layout) {
    switch (layout) {
        CASE_FROM_TO(VulkanLayout::UNDEFINED, "UNDEFINED");
        CASE_FROM_TO(VulkanLayout::STAGING, "STAGING");
        CASE_FROM_TO(VulkanLayout::FRAG_READ, "FRAG_READ");
        CASE_FROM_TO(VulkanLayout::VERT_READ, "VERT_READ");
        CASE_FROM_TO(VulkanLayout::TRANSFER_SRC, "TRANSFER_SRC");
        CASE_FROM_TO(VulkanLayout::TRANSFER_DST, "TRANSFER_DST");
        CASE_FROM_TO(VulkanLayout::DEPTH_STENCIL_ATTACHMENT, "DEPTH_STENCIL_ATTACHMENT");
        CASE_FROM_TO(VulkanLayout::DEPTH_SAMPLER, "DEPTH_SAMPLER");
        CASE_FROM_TO(VulkanLayout::PRESENT, "PRESENT");
        CASE_FROM_TO(VulkanLayout::COLOR_ATTACHMENT, "COLOR_ATTACHMENT");
        CASE_FROM_TO(VulkanLayout::COLOR_ATTACHMENT_RESOLVE, "COLOR_ATTACHMENT_RESOLVE");
    }
    return "UNKNOWN LAYOUT";
}

// 同一布局下的两种状态间无需屏障，故以 Vulkan 原生布局为键比较
constexpr inline VkImageLayout TransVulkanLayoutToVkImageLayout(VulkanLayout layout) {
    switch (layout) {
        case VulkanLayout::UNDEFINED:
            return VK_IMAGE_LAYOUT_UNDEFINED;
        case VulkanLayout::STAGING:
            return VK_IMAGE_LAYOUT_GENERAL;
        case VulkanLayout::FRAG_READ:
        case VulkanLayout::VERT_READ:
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case VulkanLayout::TRANSFER_SRC:
            return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case VulkanLayout::TRANSFER_DST:
            return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case VulkanLayout::DEPTH_STENCIL_ATTACHMENT:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case VulkanLayout::DEPTH_SAMPLER:
            return VK_IMAGE_LAYOUT_GENERAL;
        case VulkanLayout::PRESENT:
            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        // 上游会从某个 mip 层级采样的同时写入另一层级（如 bloom），因此颜色可附着纹理统一用
        // GENERAL，避免为每个层级维护独立布局
        case VulkanLayout::COLOR_ATTACHMENT:
            return VK_IMAGE_LAYOUT_GENERAL;
        case VulkanLayout::COLOR_ATTACHMENT_RESOLVE:
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    return VK_IMAGE_LAYOUT_UNDEFINED;
}

// 返回是否真的录入了转换命令——新旧布局等价时无需屏障，返回 false
bool TransitionLayout(VkCommandBuffer cmdbuffer, VulkanLayoutTransition transition);

// 上游在 Image 中实现并声明这两个函数，本项目既有 VkUtils.h 已有一份实现，故此处只保留
// 声明以兼容按上游包含路径书写的调用点，定义统一留在 VkUtils.h
bool IsVkDepthFormat(VkFormat format);

bool IsVkStencilFormat(VkFormat format);

bool IsVkYcbcrConversionFormat(VkFormat format);

VkImageAspectFlags TransVkFormatToVkImageAspectFlags(VkFormat format);

uint8_t ReduceSampleCount(uint8_t sampleCount, VkSampleCountFlags mask);

}  // namespace VK_UTILS

END_NS_BACKEND

// 日志里直接写 VulkanLayout 时按枚举名输出，复用 TransVulkanLayoutToString 的映射
template <>
struct fmt::formatter<Backend::VK_UTILS::VulkanLayout> : fmt::formatter<std::string_view> {
    template <typename Context>
    auto format(Backend::VK_UTILS::VulkanLayout layout, Context &ctx) const {
        return fmt::formatter<std::string_view>::format(Backend::VK_UTILS::TransVulkanLayoutToString(layout), ctx);
    }
};
