#pragma once

#include "Backend/DriverDefine.h"
#include "Backend/Namespace.h"
#include "vulkan/utils/Definitions.h"

#include <volk.h>

#include <cstdint>

BEGIN_NS_BACKEND

namespace VK_UTILS {

VkFormat TransElementTypeToVkFormat(ElementType type, bool normalized, bool integer);
VkFormat TransTextureFormatToVkFormat(TextureFormat format);

// 把 PixelBufferDescriptor 的「格式 + 类型」对转换为 VkFormat。
// 只返回满足 VK_FORMAT_FEATURE_BLIT_SRC_BIT 的格式——按 Vulkan 规范的必备格式支持表，
// 只有这些格式可用于格式转换；请求落在表外时返回 VK_FORMAT_UNDEFINED
VkFormat TransPixelDataFormatToVkFormat(PixelDataFormat format, PixelDataType type);

// 把 sRGB 格式折算为对应的 UNORM 格式；非 sRGB 格式原样返回。
// sRGB 与实际位布局正交，故判断是否需要基于 blit 的转换时要用它
VkFormat TransVkFormatToLinearVkFormat(VkFormat format);

// 与前端 computeTextureDataSize 的区别：那里接收对外的纹理格式，且能计入字节对齐
uint32_t GetBytesPerPixel(TextureFormat format);

// 供 staging buffer 对齐纹理行使用
uint8_t GetTexelBlockSize(VkFormat format);

VkCompareOp TransSamplerCompareFuncToVkCompareOp(SamplerCompareFunc func);
VkStencilOp TransStencilOperationToVkStencilOp(StencilOperation op);
VkBlendFactor TransBlendFunctionToVkBlendFactor(BlendFunction mode);
VkCullModeFlags TransCullingModeToVkCullModeFlags(CullingMode mode);
VkFrontFace TransInverseFrontFacesToVkFrontFace(bool inverseFrontFaces);
PixelDataType TransVkFormatToPixelDataType(VkFormat format);
uint32_t GetComponentCount(VkFormat format);
VkComponentMapping TransTextureSwizzleToVkComponentMapping(TextureSwizzle const swizzle[4]);
VkShaderStageFlags TransShaderStageFlagsToVkShaderStageFlags(ShaderStageFlags stageFlags);

// Platform 创建外部采样器时使用
VkFilter TransSamplerMinFilterToVkFilter(SamplerMinFilter filter);
VkFilter TransSamplerMagFilterToVkFilter(SamplerMagFilter filter);
VkSamplerMipmapMode TransSamplerMinFilterToVkSamplerMipmapMode(SamplerMinFilter filter);
VkSamplerAddressMode TransSamplerWrapModeToVkSamplerAddressMode(SamplerWrapMode mode);
VkBool32 TransSamplerCompareModeToVkBool32(SamplerCompareMode mode);
float GetMaxLod(SamplerMinFilter filter);

VkSamplerYcbcrModelConversion TransSamplerYcbcrModelConversionToVkSamplerYcbcrModelConversion(SamplerYcbcrModelConversion model);
VkSamplerYcbcrRange TransSamplerYcbcrRangeToVkSamplerYcbcrRange(SamplerYcbcrRange range);
VkChromaLocation TransChromaLocationToVkChromaLocation(ChromaLocation loc);

// 由 Vulkan 原生取值反查本项目类型，供回读采样器状态使用
SamplerYcbcrModelConversion TransVkSamplerYcbcrModelConversionToSamplerYcbcrModelConversion(VkSamplerYcbcrModelConversion model);
SamplerYcbcrRange TransVkSamplerYcbcrRangeToSamplerYcbcrRange(VkSamplerYcbcrRange range);
ChromaLocation TransVkChromaLocationToChromaLocation(VkChromaLocation loc);
TextureSwizzle TransVkComponentSwizzleToTextureSwizzle(VkComponentSwizzle c, uint8_t rgbaIndex);

inline VkImageViewType TransSamplerTypeToVkImageViewType(SamplerType target) {
    switch (target) {
        case SamplerType::SAMPLER_CUBEMAP:
            return VK_IMAGE_VIEW_TYPE_CUBE;
        case SamplerType::SAMPLER_2D_ARRAY:
            return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case SamplerType::SAMPLER_CUBEMAP_ARRAY:
            return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        case SamplerType::SAMPLER_3D:
            return VK_IMAGE_VIEW_TYPE_3D;
        default:
            return VK_IMAGE_VIEW_TYPE_2D;
    }
}

inline VkPrimitiveTopology TransPrimitiveTypeToVkPrimitiveTopology(PrimitiveType pt) noexcept {
    switch (pt) {
        case PrimitiveType::POINTS:
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case PrimitiveType::LINES:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveType::LINE_STRIP:
            return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case PrimitiveType::TRIANGLES:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case PrimitiveType::TRIANGLE_STRIP:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

}  // namespace VK_UTILS

END_NS_BACKEND
