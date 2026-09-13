#include "vulkan/utils/Conversion.h"

#include "Utils/Debug.h"
#include "Utils/Macro.h"
#include "Utils/Panic.h"

BEGIN_NS_BACKEND

namespace VK_UTILS {

VkFormat GetVkFormat(ElementType type, bool normalized, bool integer) {
    // 归一化格式只存在于 8/16 位类型，且不区分整数与缩放
    if (normalized) {
        switch (type) {
            CASE_FROM_TO(ElementType::BYTE, VK_FORMAT_R8_SNORM);
            CASE_FROM_TO(ElementType::UBYTE, VK_FORMAT_R8_UNORM);
            CASE_FROM_TO(ElementType::SHORT, VK_FORMAT_R16_SNORM);
            CASE_FROM_TO(ElementType::USHORT, VK_FORMAT_R16_UNORM);
            CASE_FROM_TO(ElementType::BYTE2, VK_FORMAT_R8G8_SNORM);
            CASE_FROM_TO(ElementType::UBYTE2, VK_FORMAT_R8G8_UNORM);
            CASE_FROM_TO(ElementType::SHORT2, VK_FORMAT_R16G16_SNORM);
            CASE_FROM_TO(ElementType::USHORT2, VK_FORMAT_R16G16_UNORM);
            CASE_FROM_TO(ElementType::BYTE3, VK_FORMAT_R8G8B8_SNORM);
            CASE_FROM_TO(ElementType::UBYTE3, VK_FORMAT_R8G8B8_UNORM);
            CASE_FROM_TO(ElementType::SHORT3, VK_FORMAT_R16G16B16_SNORM);
            CASE_FROM_TO(ElementType::USHORT3, VK_FORMAT_R16G16B16_UNORM);
            CASE_FROM_TO(ElementType::BYTE4, VK_FORMAT_R8G8B8A8_SNORM);
            CASE_FROM_TO(ElementType::UBYTE4, VK_FORMAT_R8G8B8A8_UNORM);
            CASE_FROM_TO(ElementType::SHORT4, VK_FORMAT_R16G16B16A16_SNORM);
            CASE_FROM_TO(ElementType::USHORT4, VK_FORMAT_R16G16B16A16_UNORM);
            default:
                FILAMENT_CHECK_POSTCONDITION(false) << "Normalized format does not exist.";
                return VK_FORMAT_UNDEFINED;
        }
    }

    switch (type) {
        CASE_FROM_TO(ElementType::BYTE, integer ? VK_FORMAT_R8_SINT : VK_FORMAT_R8_SSCALED);
        CASE_FROM_TO(ElementType::UBYTE, integer ? VK_FORMAT_R8_UINT : VK_FORMAT_R8_USCALED);
        CASE_FROM_TO(ElementType::SHORT, integer ? VK_FORMAT_R16_SINT : VK_FORMAT_R16_SSCALED);
        CASE_FROM_TO(ElementType::USHORT, integer ? VK_FORMAT_R16_UINT : VK_FORMAT_R16_USCALED);
        CASE_FROM_TO(ElementType::HALF, VK_FORMAT_R16_SFLOAT);
        CASE_FROM_TO(ElementType::INT, VK_FORMAT_R32_SINT);
        CASE_FROM_TO(ElementType::UINT, VK_FORMAT_R32_UINT);
        CASE_FROM_TO(ElementType::FLOAT, VK_FORMAT_R32_SFLOAT);
        CASE_FROM_TO(ElementType::BYTE2, integer ? VK_FORMAT_R8G8_SINT : VK_FORMAT_R8G8_SSCALED);
        CASE_FROM_TO(ElementType::UBYTE2, integer ? VK_FORMAT_R8G8_UINT : VK_FORMAT_R8G8_USCALED);
        CASE_FROM_TO(ElementType::SHORT2, integer ? VK_FORMAT_R16G16_SINT : VK_FORMAT_R16G16_SSCALED);
        CASE_FROM_TO(ElementType::USHORT2, integer ? VK_FORMAT_R16G16_UINT : VK_FORMAT_R16G16_USCALED);
        CASE_FROM_TO(ElementType::HALF2, VK_FORMAT_R16G16_SFLOAT);
        CASE_FROM_TO(ElementType::FLOAT2, VK_FORMAT_R32G32_SFLOAT);
        CASE_FROM_TO(ElementType::BYTE3, integer ? VK_FORMAT_R8G8B8_SINT : VK_FORMAT_R8G8B8_SSCALED);
        CASE_FROM_TO(ElementType::UBYTE3, integer ? VK_FORMAT_R8G8B8_UINT : VK_FORMAT_R8G8B8_USCALED);
        CASE_FROM_TO(ElementType::SHORT3, integer ? VK_FORMAT_R16G16B16_SINT : VK_FORMAT_R16G16B16_SSCALED);
        CASE_FROM_TO(ElementType::USHORT3, integer ? VK_FORMAT_R16G16B16_UINT : VK_FORMAT_R16G16B16_USCALED);
        CASE_FROM_TO(ElementType::HALF3, VK_FORMAT_R16G16B16_SFLOAT);
        CASE_FROM_TO(ElementType::FLOAT3, VK_FORMAT_R32G32B32_SFLOAT);
        CASE_FROM_TO(ElementType::BYTE4, integer ? VK_FORMAT_R8G8B8A8_SINT : VK_FORMAT_R8G8B8A8_SSCALED);
        CASE_FROM_TO(ElementType::UBYTE4, integer ? VK_FORMAT_R8G8B8A8_UINT : VK_FORMAT_R8G8B8A8_USCALED);
        CASE_FROM_TO(ElementType::SHORT4, integer ? VK_FORMAT_R16G16B16A16_SINT : VK_FORMAT_R16G16B16A16_SSCALED);
        CASE_FROM_TO(ElementType::USHORT4, integer ? VK_FORMAT_R16G16B16A16_UINT : VK_FORMAT_R16G16B16A16_USCALED);
        CASE_FROM_TO(ElementType::HALF4, VK_FORMAT_R16G16B16A16_SFLOAT);
        CASE_FROM_TO(ElementType::FLOAT4, VK_FORMAT_R32G32B32A32_SFLOAT);
    }
    return VK_FORMAT_UNDEFINED;
}

VkFormat GetVkFormat(TextureFormat format) {
    switch (format) {
        // 每元素 8 位
        CASE_FROM_TO(TextureFormat::R8, VK_FORMAT_R8_UNORM);
        CASE_FROM_TO(TextureFormat::R8_SNORM, VK_FORMAT_R8_SNORM);
        CASE_FROM_TO(TextureFormat::R8UI, VK_FORMAT_R8_UINT);
        CASE_FROM_TO(TextureFormat::R8I, VK_FORMAT_R8_SINT);
        CASE_FROM_TO(TextureFormat::STENCIL8, VK_FORMAT_S8_UINT);

        // 每元素 16 位
        CASE_FROM_TO(TextureFormat::R16F, VK_FORMAT_R16_SFLOAT);
        CASE_FROM_TO(TextureFormat::R16UI, VK_FORMAT_R16_UINT);
        CASE_FROM_TO(TextureFormat::R16I, VK_FORMAT_R16_SINT);
        CASE_FROM_TO(TextureFormat::RG8, VK_FORMAT_R8G8_UNORM);
        CASE_FROM_TO(TextureFormat::RG8_SNORM, VK_FORMAT_R8G8_SNORM);
        CASE_FROM_TO(TextureFormat::RG8UI, VK_FORMAT_R8G8_UINT);
        CASE_FROM_TO(TextureFormat::RG8I, VK_FORMAT_R8G8_SINT);
        CASE_FROM_TO(TextureFormat::RGB565, VK_FORMAT_R5G6B5_UNORM_PACK16);
        CASE_FROM_TO(TextureFormat::RGB9_E5, VK_FORMAT_E5B9G9R9_UFLOAT_PACK32);
        CASE_FROM_TO(TextureFormat::RGB5_A1, VK_FORMAT_R5G5B5A1_UNORM_PACK16);
        CASE_FROM_TO(TextureFormat::RGBA4, VK_FORMAT_R4G4B4A4_UNORM_PACK16);
        CASE_FROM_TO(TextureFormat::DEPTH16, VK_FORMAT_D16_UNORM);

        // 每元素 24 位；实际支持该位宽的 GPU 极少，故统一整形为 32 位格式
        CASE_FROM_TO(TextureFormat::RGB8, VK_FORMAT_R8G8B8A8_UNORM);
        CASE_FROM_TO(TextureFormat::SRGB8, VK_FORMAT_R8G8B8A8_SRGB);
        CASE_FROM_TO(TextureFormat::RGB8_SNORM, VK_FORMAT_R8G8B8A8_SNORM);
        CASE_FROM_TO(TextureFormat::RGB8UI, VK_FORMAT_R8G8B8A8_UINT);
        CASE_FROM_TO(TextureFormat::RGB8I, VK_FORMAT_R8G8B8A8_SINT);

        // 32 位格式，其中 8 位未使用
        CASE_FROM_TO(TextureFormat::DEPTH24, VK_FORMAT_X8_D24_UNORM_PACK32);

        // 每元素 32 位
        CASE_FROM_TO(TextureFormat::R32F, VK_FORMAT_R32_SFLOAT);
        CASE_FROM_TO(TextureFormat::R32UI, VK_FORMAT_R32_UINT);
        CASE_FROM_TO(TextureFormat::R32I, VK_FORMAT_R32_SINT);
        CASE_FROM_TO(TextureFormat::RG16F, VK_FORMAT_R16G16_SFLOAT);
        CASE_FROM_TO(TextureFormat::RG16UI, VK_FORMAT_R16G16_UINT);
        CASE_FROM_TO(TextureFormat::RG16I, VK_FORMAT_R16G16_SINT);
        CASE_FROM_TO(TextureFormat::R11F_G11F_B10F, VK_FORMAT_B10G11R11_UFLOAT_PACK32);
        CASE_FROM_TO(TextureFormat::RGBA8, VK_FORMAT_R8G8B8A8_UNORM);
        CASE_FROM_TO(TextureFormat::SRGB8_A8, VK_FORMAT_R8G8B8A8_SRGB);
        CASE_FROM_TO(TextureFormat::RGBA8_SNORM, VK_FORMAT_R8G8B8A8_SNORM);
        CASE_FROM_TO(TextureFormat::RGB10_A2, VK_FORMAT_A2B10G10R10_UNORM_PACK32);
        CASE_FROM_TO(TextureFormat::RGBA8UI, VK_FORMAT_R8G8B8A8_UINT);
        CASE_FROM_TO(TextureFormat::RGBA8I, VK_FORMAT_R8G8B8A8_SINT);
        CASE_FROM_TO(TextureFormat::DEPTH32F, VK_FORMAT_D32_SFLOAT);
        CASE_FROM_TO(TextureFormat::DEPTH24_STENCIL8, VK_FORMAT_D24_UNORM_S8_UINT);
        CASE_FROM_TO(TextureFormat::DEPTH32F_STENCIL8, VK_FORMAT_D32_SFLOAT_S8_UINT);

        // 每元素 48 位；同 24 位格式，统一整形为 64 位
        CASE_FROM_TO(TextureFormat::RGB16F, VK_FORMAT_R16G16B16A16_SFLOAT);
        CASE_FROM_TO(TextureFormat::RGB16UI, VK_FORMAT_R16G16B16A16_UINT);
        CASE_FROM_TO(TextureFormat::RGB16I, VK_FORMAT_R16G16B16A16_SINT);

        // 每元素 64 位
        CASE_FROM_TO(TextureFormat::RG32F, VK_FORMAT_R32G32_SFLOAT);
        CASE_FROM_TO(TextureFormat::RG32UI, VK_FORMAT_R32G32_UINT);
        CASE_FROM_TO(TextureFormat::RG32I, VK_FORMAT_R32G32_SINT);
        CASE_FROM_TO(TextureFormat::RGBA16F, VK_FORMAT_R16G16B16A16_SFLOAT);
        CASE_FROM_TO(TextureFormat::RGBA16UI, VK_FORMAT_R16G16B16A16_UINT);
        CASE_FROM_TO(TextureFormat::RGBA16I, VK_FORMAT_R16G16B16A16_SINT);

        // 每元素 96 位；同 24 位格式，统一整形为 128 位
        CASE_FROM_TO(TextureFormat::RGB32F, VK_FORMAT_R32G32B32A32_SFLOAT);
        CASE_FROM_TO(TextureFormat::RGB32UI, VK_FORMAT_R32G32B32A32_UINT);
        CASE_FROM_TO(TextureFormat::RGB32I, VK_FORMAT_R32G32B32A32_SINT);

        // 每元素 128 位
        CASE_FROM_TO(TextureFormat::RGBA32F, VK_FORMAT_R32G32B32A32_SFLOAT);
        CASE_FROM_TO(TextureFormat::RGBA32UI, VK_FORMAT_R32G32B32A32_UINT);
        CASE_FROM_TO(TextureFormat::RGBA32I, VK_FORMAT_R32G32B32A32_SINT);

        // 压缩格式
        CASE_FROM_TO(TextureFormat::DXT1_RGB, VK_FORMAT_BC1_RGB_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::DXT1_SRGB, VK_FORMAT_BC1_RGB_SRGB_BLOCK);
        CASE_FROM_TO(TextureFormat::DXT1_RGBA, VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::DXT1_SRGBA, VK_FORMAT_BC1_RGBA_SRGB_BLOCK);
        CASE_FROM_TO(TextureFormat::DXT3_RGBA, VK_FORMAT_BC2_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::DXT3_SRGBA, VK_FORMAT_BC2_SRGB_BLOCK);
        CASE_FROM_TO(TextureFormat::DXT5_RGBA, VK_FORMAT_BC3_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::DXT5_SRGBA, VK_FORMAT_BC3_SRGB_BLOCK);

        CASE_FROM_TO(TextureFormat::RED_RGTC1, VK_FORMAT_BC4_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::SIGNED_RED_RGTC1, VK_FORMAT_BC4_SNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::RED_GREEN_RGTC2, VK_FORMAT_BC5_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::SIGNED_RED_GREEN_RGTC2, VK_FORMAT_BC5_SNORM_BLOCK);

        CASE_FROM_TO(TextureFormat::RGB_BPTC_SIGNED_FLOAT, VK_FORMAT_BC6H_SFLOAT_BLOCK);
        CASE_FROM_TO(TextureFormat::RGB_BPTC_UNSIGNED_FLOAT, VK_FORMAT_BC6H_UFLOAT_BLOCK);
        CASE_FROM_TO(TextureFormat::RGBA_BPTC_UNORM, VK_FORMAT_BC7_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::SRGB_ALPHA_BPTC_UNORM, VK_FORMAT_BC7_SRGB_BLOCK);

        CASE_FROM_TO(TextureFormat::RGBA_ASTC_4x4, VK_FORMAT_ASTC_4x4_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::RGBA_ASTC_5x4, VK_FORMAT_ASTC_5x4_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::RGBA_ASTC_5x5, VK_FORMAT_ASTC_5x5_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::RGBA_ASTC_6x5, VK_FORMAT_ASTC_6x5_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::RGBA_ASTC_6x6, VK_FORMAT_ASTC_6x6_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::RGBA_ASTC_8x5, VK_FORMAT_ASTC_8x5_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::RGBA_ASTC_8x6, VK_FORMAT_ASTC_8x6_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::RGBA_ASTC_8x8, VK_FORMAT_ASTC_8x8_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::RGBA_ASTC_10x5, VK_FORMAT_ASTC_10x5_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::RGBA_ASTC_10x6, VK_FORMAT_ASTC_10x6_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::RGBA_ASTC_10x8, VK_FORMAT_ASTC_10x8_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::RGBA_ASTC_10x10, VK_FORMAT_ASTC_10x10_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::RGBA_ASTC_12x10, VK_FORMAT_ASTC_12x10_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::RGBA_ASTC_12x12, VK_FORMAT_ASTC_12x12_UNORM_BLOCK);

        CASE_FROM_TO(TextureFormat::SRGB8_ALPHA8_ASTC_4x4, VK_FORMAT_ASTC_4x4_SRGB_BLOCK);
        CASE_FROM_TO(TextureFormat::SRGB8_ALPHA8_ASTC_5x4, VK_FORMAT_ASTC_5x4_SRGB_BLOCK);
        CASE_FROM_TO(TextureFormat::SRGB8_ALPHA8_ASTC_5x5, VK_FORMAT_ASTC_5x5_SRGB_BLOCK);
        CASE_FROM_TO(TextureFormat::SRGB8_ALPHA8_ASTC_6x5, VK_FORMAT_ASTC_6x5_SRGB_BLOCK);
        CASE_FROM_TO(TextureFormat::SRGB8_ALPHA8_ASTC_6x6, VK_FORMAT_ASTC_6x6_SRGB_BLOCK);
        CASE_FROM_TO(TextureFormat::SRGB8_ALPHA8_ASTC_8x5, VK_FORMAT_ASTC_8x5_SRGB_BLOCK);
        CASE_FROM_TO(TextureFormat::SRGB8_ALPHA8_ASTC_8x6, VK_FORMAT_ASTC_8x6_SRGB_BLOCK);
        CASE_FROM_TO(TextureFormat::SRGB8_ALPHA8_ASTC_8x8, VK_FORMAT_ASTC_8x8_SRGB_BLOCK);
        CASE_FROM_TO(TextureFormat::SRGB8_ALPHA8_ASTC_10x5, VK_FORMAT_ASTC_10x5_SRGB_BLOCK);
        CASE_FROM_TO(TextureFormat::SRGB8_ALPHA8_ASTC_10x6, VK_FORMAT_ASTC_10x6_SRGB_BLOCK);
        CASE_FROM_TO(TextureFormat::SRGB8_ALPHA8_ASTC_10x8, VK_FORMAT_ASTC_10x8_SRGB_BLOCK);
        CASE_FROM_TO(TextureFormat::SRGB8_ALPHA8_ASTC_10x10, VK_FORMAT_ASTC_10x10_SRGB_BLOCK);
        CASE_FROM_TO(TextureFormat::SRGB8_ALPHA8_ASTC_12x10, VK_FORMAT_ASTC_12x10_SRGB_BLOCK);
        CASE_FROM_TO(TextureFormat::SRGB8_ALPHA8_ASTC_12x12, VK_FORMAT_ASTC_12x12_SRGB_BLOCK);

        CASE_FROM_TO(TextureFormat::ETC2_RGB8, VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::ETC2_SRGB8, VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK);
        CASE_FROM_TO(TextureFormat::ETC2_RGB8_A1, VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::ETC2_SRGB8_A1, VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK);

        CASE_FROM_TO(TextureFormat::ETC2_EAC_RGBA8, VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::ETC2_EAC_SRGBA8, VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK);

        CASE_FROM_TO(TextureFormat::EAC_R11, VK_FORMAT_EAC_R11_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::EAC_R11_SIGNED, VK_FORMAT_EAC_R11_SNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::EAC_RG11, VK_FORMAT_EAC_R11G11_UNORM_BLOCK);
        CASE_FROM_TO(TextureFormat::EAC_RG11_SIGNED, VK_FORMAT_EAC_R11G11_SNORM_BLOCK);

        // UNUSED 是历史上 rgbm 格式留下的占位值，无对应 Vulkan 格式
        CASE_FROM_TO(TextureFormat::UNUSED, VK_FORMAT_UNDEFINED);
    }
    return VK_FORMAT_UNDEFINED;
}

// 依据 https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#formats-compatibility-classes
uint8_t GetTexelBlockSize(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R4G4_UNORM_PACK8:
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R8_SNORM:
        case VK_FORMAT_R8_USCALED:
        case VK_FORMAT_R8_SSCALED:
        case VK_FORMAT_R8_UINT:
        case VK_FORMAT_R8_SINT:
        case VK_FORMAT_R8_SRGB:
        case VK_FORMAT_S8_UINT:
            return 1;

        case VK_FORMAT_R10X6_UNORM_PACK16:
        case VK_FORMAT_R12X4_UNORM_PACK16:
        case VK_FORMAT_A4R4G4B4_UNORM_PACK16:
        case VK_FORMAT_A4B4G4R4_UNORM_PACK16:
        case VK_FORMAT_R4G4B4A4_UNORM_PACK16:
        case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
        case VK_FORMAT_R5G6B5_UNORM_PACK16:
        case VK_FORMAT_B5G6R5_UNORM_PACK16:
        case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
        case VK_FORMAT_B5G5R5A1_UNORM_PACK16:
        case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
        case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_R8G8_SNORM:
        case VK_FORMAT_R8G8_USCALED:
        case VK_FORMAT_R8G8_SSCALED:
        case VK_FORMAT_R8G8_UINT:
        case VK_FORMAT_R8G8_SINT:
        case VK_FORMAT_R8G8_SRGB:
        case VK_FORMAT_R16_UNORM:
        case VK_FORMAT_R16_SNORM:
        case VK_FORMAT_R16_USCALED:
        case VK_FORMAT_R16_SSCALED:
        case VK_FORMAT_R16_UINT:
        case VK_FORMAT_R16_SINT:
        case VK_FORMAT_R16_SFLOAT:
        case VK_FORMAT_D16_UNORM:
            return 2;

        case VK_FORMAT_R8G8B8_UNORM:
        case VK_FORMAT_R8G8B8_SNORM:
        case VK_FORMAT_R8G8B8_USCALED:
        case VK_FORMAT_R8G8B8_SSCALED:
        case VK_FORMAT_R8G8B8_UINT:
        case VK_FORMAT_R8G8B8_SINT:
        case VK_FORMAT_R8G8B8_SRGB:
        case VK_FORMAT_B8G8R8_UNORM:
        case VK_FORMAT_B8G8R8_SNORM:
        case VK_FORMAT_B8G8R8_USCALED:
        case VK_FORMAT_B8G8R8_SSCALED:
        case VK_FORMAT_B8G8R8_UINT:
        case VK_FORMAT_B8G8R8_SINT:
        case VK_FORMAT_B8G8R8_SRGB:
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
        case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
        case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:
        case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM:
        case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM:
        case VK_FORMAT_G8_B8R8_2PLANE_444_UNORM:
            return 3;

        case VK_FORMAT_R10X6G10X6_UNORM_2PACK16:
        case VK_FORMAT_R12X4G12X4_UNORM_2PACK16:
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SNORM:
        case VK_FORMAT_R8G8B8A8_USCALED:
        case VK_FORMAT_R8G8B8A8_SSCALED:
        case VK_FORMAT_R8G8B8A8_UINT:
        case VK_FORMAT_R8G8B8A8_SINT:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SNORM:
        case VK_FORMAT_B8G8R8A8_USCALED:
        case VK_FORMAT_B8G8R8A8_SSCALED:
        case VK_FORMAT_B8G8R8A8_UINT:
        case VK_FORMAT_B8G8R8A8_SINT:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
        case VK_FORMAT_A8B8G8R8_SNORM_PACK32:
        case VK_FORMAT_A8B8G8R8_USCALED_PACK32:
        case VK_FORMAT_A8B8G8R8_SSCALED_PACK32:
        case VK_FORMAT_A8B8G8R8_UINT_PACK32:
        case VK_FORMAT_A8B8G8R8_SINT_PACK32:
        case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
        case VK_FORMAT_A2R10G10B10_SNORM_PACK32:
        case VK_FORMAT_A2R10G10B10_USCALED_PACK32:
        case VK_FORMAT_A2R10G10B10_SSCALED_PACK32:
        case VK_FORMAT_A2R10G10B10_UINT_PACK32:
        case VK_FORMAT_A2R10G10B10_SINT_PACK32:
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
        case VK_FORMAT_A2B10G10R10_USCALED_PACK32:
        case VK_FORMAT_A2B10G10R10_SSCALED_PACK32:
        case VK_FORMAT_A2B10G10R10_UINT_PACK32:
        case VK_FORMAT_A2B10G10R10_SINT_PACK32:
        case VK_FORMAT_R16G16_UNORM:
        case VK_FORMAT_R16G16_SNORM:
        case VK_FORMAT_R16G16_USCALED:
        case VK_FORMAT_R16G16_SSCALED:
        case VK_FORMAT_R16G16_UINT:
        case VK_FORMAT_R16G16_SINT:
        case VK_FORMAT_R16G16_SFLOAT:
        case VK_FORMAT_R32_UINT:
        case VK_FORMAT_R32_SINT:
        case VK_FORMAT_R32_SFLOAT:
        case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
        case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_G8B8G8R8_422_UNORM:
        case VK_FORMAT_B8G8R8G8_422_UNORM:
            return 4;

        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return 5;

        case VK_FORMAT_R16G16B16_UNORM:
        case VK_FORMAT_R16G16B16_SNORM:
        case VK_FORMAT_R16G16B16_USCALED:
        case VK_FORMAT_R16G16B16_SSCALED:
        case VK_FORMAT_R16G16B16_UINT:
        case VK_FORMAT_R16G16B16_SINT:
        case VK_FORMAT_R16G16B16_SFLOAT:
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16:
        case VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM:
        case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM:
        case VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM:
        case VK_FORMAT_G16_B16R16_2PLANE_422_UNORM:
        case VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM:
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16:
        case VK_FORMAT_G16_B16R16_2PLANE_444_UNORM:
            return 6;

        case VK_FORMAT_R16G16B16A16_UNORM:
        case VK_FORMAT_R16G16B16A16_SNORM:
        case VK_FORMAT_R16G16B16A16_USCALED:
        case VK_FORMAT_R16G16B16A16_SSCALED:
        case VK_FORMAT_R16G16B16A16_UINT:
        case VK_FORMAT_R16G16B16A16_SINT:
        case VK_FORMAT_R16G16B16A16_SFLOAT:
        case VK_FORMAT_R32G32_UINT:
        case VK_FORMAT_R32G32_SINT:
        case VK_FORMAT_R32G32_SFLOAT:
        case VK_FORMAT_R64_UINT:
        case VK_FORMAT_R64_SINT:
        case VK_FORMAT_R64_SFLOAT:
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
        case VK_FORMAT_EAC_R11_UNORM_BLOCK:
        case VK_FORMAT_EAC_R11_SNORM_BLOCK:
        case VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16:
        case VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16:
        case VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16:
        case VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16:
        case VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16:
        case VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16:
        case VK_FORMAT_G16B16G16R16_422_UNORM:
        case VK_FORMAT_B16G16R16G16_422_UNORM:
        case VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG:
        case VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG:
        case VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG:
        case VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG:
        case VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG:
        case VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG:
        case VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG:
        case VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG:
            return 8;

        case VK_FORMAT_R32G32B32_UINT:
        case VK_FORMAT_R32G32B32_SINT:
        case VK_FORMAT_R32G32B32_SFLOAT:
            return 12;

        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
        case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
        case VK_FORMAT_EAC_R11G11_UNORM_BLOCK:
        case VK_FORMAT_EAC_R11G11_SNORM_BLOCK:
        case VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
        case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
        case VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_5x4_UNORM_BLOCK:
        case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:
        case VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
        case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
        case VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_6x5_UNORM_BLOCK:
        case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:
        case VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
        case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
        case VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_8x5_UNORM_BLOCK:
        case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:
        case VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_8x6_UNORM_BLOCK:
        case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:
        case VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
        case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
        case VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_10x5_UNORM_BLOCK:
        case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:
        case VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_10x6_UNORM_BLOCK:
        case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:
        case VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_10x8_UNORM_BLOCK:
        case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:
        case VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_10x10_UNORM_BLOCK:
        case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
        case VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_12x10_UNORM_BLOCK:
        case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:
        case VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK:
        case VK_FORMAT_ASTC_12x12_UNORM_BLOCK:
        case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:
        case VK_FORMAT_R32G32B32A32_UINT:
        case VK_FORMAT_R32G32B32A32_SINT:
        case VK_FORMAT_R32G32B32A32_SFLOAT:
        case VK_FORMAT_R64G64_UINT:
        case VK_FORMAT_R64G64_SINT:
        case VK_FORMAT_R64G64_SFLOAT:
            return 16;

        case VK_FORMAT_R64G64B64_UINT:
        case VK_FORMAT_R64G64B64_SINT:
        case VK_FORMAT_R64G64B64_SFLOAT:
            return 24;

        case VK_FORMAT_R64G64B64A64_UINT:
        case VK_FORMAT_R64G64B64A64_SINT:
        case VK_FORMAT_R64G64B64A64_SFLOAT:
            return 32;

        case VK_FORMAT_UNDEFINED:
            // 已明确判定格式不受支持时交由上层处理，返回 1 表示无需对齐
            return 1;

        default:
            // 走到这里说明 GetVkFormat 新增了格式而本函数未同步，属实现缺陷
            assert_invariant(false && "Unknown data type, conversion is not supported.");
            return 0;
    }
}

VkFormat GetVkFormat(PixelDataFormat format, PixelDataType type) {
    // 这三个类型本身就唯一确定了位布局，与 format 无关，故必须先于 format 判定
    if (type == PixelDataType::USHORT_565) return VK_FORMAT_R5G6B5_UNORM_PACK16;
    if (type == PixelDataType::UINT_2_10_10_10_REV) return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    if (type == PixelDataType::UINT_10F_11F_11F_REV) return VK_FORMAT_B10G11R11_UFLOAT_PACK32;

    switch (format) {
        CASE_FROM_TO(PixelDataFormat::R, type == PixelDataType::UBYTE ? VK_FORMAT_R8_UNORM
                                                   : type == PixelDataType::BYTE ? VK_FORMAT_R8_SNORM
                                                   : type == PixelDataType::HALF ? VK_FORMAT_R16_SFLOAT
                                                                                 : VK_FORMAT_UNDEFINED);
        CASE_FROM_TO(PixelDataFormat::R_INTEGER, type == PixelDataType::UBYTE ? VK_FORMAT_R8_UINT
                                                           : type == PixelDataType::BYTE ? VK_FORMAT_R8_SINT
                                                           : type == PixelDataType::USHORT ? VK_FORMAT_R16_UINT
                                                           : type == PixelDataType::SHORT ? VK_FORMAT_R16_SINT
                                                           : type == PixelDataType::UINT ? VK_FORMAT_R32_UINT
                                                                                         : VK_FORMAT_R32_SINT);
        CASE_FROM_TO(PixelDataFormat::RG, type == PixelDataType::UBYTE ? VK_FORMAT_R8G8_UNORM
                                                    : type == PixelDataType::BYTE ? VK_FORMAT_R8G8_SNORM
                                                    : type == PixelDataType::HALF ? VK_FORMAT_R16G16_SFLOAT
                                                                                  : VK_FORMAT_R32G32_SFLOAT);
        CASE_FROM_TO(PixelDataFormat::RG_INTEGER, type == PixelDataType::UBYTE ? VK_FORMAT_R8G8_UINT
                                                            : type == PixelDataType::BYTE ? VK_FORMAT_R8G8_SINT
                                                            : type == PixelDataType::USHORT ? VK_FORMAT_R16G16_UINT
                                                            : type == PixelDataType::SHORT ? VK_FORMAT_R16G16_SINT
                                                            : type == PixelDataType::UINT ? VK_FORMAT_R32G32_UINT
                                                                                          : VK_FORMAT_R32G32_SINT);
        CASE_FROM_TO(PixelDataFormat::RGBA, type == PixelDataType::UBYTE ? VK_FORMAT_R8G8B8A8_UNORM
                                                      : type == PixelDataType::BYTE ? VK_FORMAT_R8G8B8A8_SNORM
                                                      : type == PixelDataType::HALF ? VK_FORMAT_R16G16B16A16_SFLOAT
                                                                                    : VK_FORMAT_R32G32B32A32_SFLOAT);
        CASE_FROM_TO(PixelDataFormat::RGBA_INTEGER,
                     type == PixelDataType::UBYTE ? VK_FORMAT_R8G8B8A8_UINT
                     : type == PixelDataType::BYTE ? VK_FORMAT_R8G8B8A8_SINT
                     : type == PixelDataType::USHORT ? VK_FORMAT_R16G16B16A16_UINT
                     : type == PixelDataType::SHORT ? VK_FORMAT_R16G16B16A16_SINT
                     : type == PixelDataType::UINT ? VK_FORMAT_R32G32B32A32_UINT
                                                   : VK_FORMAT_R32G32B32A32_SINT);
        CASE_FROM_TO(PixelDataFormat::RGB, type == PixelDataType::UBYTE ? VK_FORMAT_R8G8B8A8_UNORM
                                                     : type == PixelDataType::BYTE ? VK_FORMAT_R8G8B8A8_SNORM
                                                                                   : VK_FORMAT_UNDEFINED);
        CASE_FROM_TO(PixelDataFormat::RGB_INTEGER, type == PixelDataType::UBYTE ? VK_FORMAT_R8G8B8A8_UINT
                                                             : type == PixelDataType::BYTE ? VK_FORMAT_R8G8B8A8_SINT
                                                                                           : VK_FORMAT_UNDEFINED);
        CASE_FROM_TO(PixelDataFormat::ALPHA, type == PixelDataType::UBYTE ? VK_FORMAT_R8_UNORM
                                                       : type == PixelDataType::BYTE ? VK_FORMAT_R8_SNORM
                                                                                     : VK_FORMAT_UNDEFINED);
        CASE_FROM_TO(PixelDataFormat::DEPTH_COMPONENT, type == PixelDataType::USHORT ? VK_FORMAT_D16_UNORM
                                                                   : VK_FORMAT_D32_SFLOAT);
        // 24 位深度 + 8 位模板只有合并格式一种表达
        CASE_FROM_TO(PixelDataFormat::DEPTH_STENCIL, VK_FORMAT_D24_UNORM_S8_UINT);
        CASE_FROM_TO(PixelDataFormat::UNUSED, VK_FORMAT_UNDEFINED);
    }

    return VK_FORMAT_UNDEFINED;
}

VkFormat GetVkFormatLinear(VkFormat format) {
    switch (format) {
        CASE_FROM_TO(VK_FORMAT_R8_SRGB, VK_FORMAT_R8_UNORM);
        CASE_FROM_TO(VK_FORMAT_R8G8_SRGB, VK_FORMAT_R8G8_UNORM);
        CASE_FROM_TO(VK_FORMAT_R8G8B8_SRGB, VK_FORMAT_R8G8B8_UNORM);
        CASE_FROM_TO(VK_FORMAT_B8G8R8_SRGB, VK_FORMAT_B8G8R8_UNORM);
        CASE_FROM_TO(VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM);
        CASE_FROM_TO(VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM);
        CASE_FROM_TO(VK_FORMAT_A8B8G8R8_SRGB_PACK32, VK_FORMAT_A8B8G8R8_UNORM_PACK32);
        CASE_FROM_TO(VK_FORMAT_BC1_RGB_SRGB_BLOCK, VK_FORMAT_BC1_RGB_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_BC1_RGBA_SRGB_BLOCK, VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_BC2_SRGB_BLOCK, VK_FORMAT_BC2_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_BC3_SRGB_BLOCK, VK_FORMAT_BC3_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_BC7_SRGB_BLOCK, VK_FORMAT_BC7_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK, VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK, VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK, VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_ASTC_4x4_SRGB_BLOCK, VK_FORMAT_ASTC_4x4_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_ASTC_5x4_SRGB_BLOCK, VK_FORMAT_ASTC_5x4_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_ASTC_5x5_SRGB_BLOCK, VK_FORMAT_ASTC_5x5_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_ASTC_6x5_SRGB_BLOCK, VK_FORMAT_ASTC_6x5_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_ASTC_6x6_SRGB_BLOCK, VK_FORMAT_ASTC_6x6_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_ASTC_8x5_SRGB_BLOCK, VK_FORMAT_ASTC_8x5_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_ASTC_8x6_SRGB_BLOCK, VK_FORMAT_ASTC_8x6_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_ASTC_8x8_SRGB_BLOCK, VK_FORMAT_ASTC_8x8_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_ASTC_10x5_SRGB_BLOCK, VK_FORMAT_ASTC_10x5_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_ASTC_10x6_SRGB_BLOCK, VK_FORMAT_ASTC_10x6_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_ASTC_10x8_SRGB_BLOCK, VK_FORMAT_ASTC_10x8_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_ASTC_10x10_SRGB_BLOCK, VK_FORMAT_ASTC_10x10_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_ASTC_12x10_SRGB_BLOCK, VK_FORMAT_ASTC_12x10_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_ASTC_12x12_SRGB_BLOCK, VK_FORMAT_ASTC_12x12_UNORM_BLOCK);
        CASE_FROM_TO(VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG, VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG);
        CASE_FROM_TO(VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG, VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG);
        CASE_FROM_TO(VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG, VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG);
        CASE_FROM_TO(VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG, VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG);
        default:
            return format;
    }
}

uint32_t GetBytesPerPixel(TextureFormat format) {
    // 直接按枚举编排分组：这里既要给出各格式的字节数，也要对 TextureFormat 的取值做穷举核对
    switch (format) {
        case TextureFormat::R8:
        case TextureFormat::R8_SNORM:
        case TextureFormat::R8UI:
        case TextureFormat::R8I:
        case TextureFormat::STENCIL8:
            return 1;

        case TextureFormat::R16F:
        case TextureFormat::R16UI:
        case TextureFormat::R16I:
        case TextureFormat::RG8:
        case TextureFormat::RG8_SNORM:
        case TextureFormat::RG8UI:
        case TextureFormat::RG8I:
        case TextureFormat::RGB565:
        case TextureFormat::RGB5_A1:
        case TextureFormat::RGBA4:
        case TextureFormat::DEPTH16:
            return 2;

        case TextureFormat::RGB8:
        case TextureFormat::SRGB8:
        case TextureFormat::RGB8_SNORM:
        case TextureFormat::RGB8UI:
        case TextureFormat::RGB8I:
        case TextureFormat::DEPTH24:
            return 3;

        case TextureFormat::R32F:
        case TextureFormat::R32UI:
        case TextureFormat::R32I:
        case TextureFormat::RG16F:
        case TextureFormat::RG16UI:
        case TextureFormat::RG16I:
        case TextureFormat::R11F_G11F_B10F:
        case TextureFormat::RGB9_E5:
        case TextureFormat::RGBA8:
        case TextureFormat::SRGB8_A8:
        case TextureFormat::RGBA8_SNORM:
        case TextureFormat::RGB10_A2:
        case TextureFormat::RGBA8UI:
        case TextureFormat::RGBA8I:
        case TextureFormat::DEPTH32F:
        case TextureFormat::DEPTH24_STENCIL8:
        case TextureFormat::DEPTH32F_STENCIL8:
            return 4;

        case TextureFormat::RGB16F:
        case TextureFormat::RGB16UI:
        case TextureFormat::RGB16I:
            return 6;

        case TextureFormat::RG32F:
        case TextureFormat::RG32UI:
        case TextureFormat::RG32I:
        case TextureFormat::RGBA16F:
        case TextureFormat::RGBA16UI:
        case TextureFormat::RGBA16I:
            return 8;

        case TextureFormat::RGB32F:
        case TextureFormat::RGB32UI:
        case TextureFormat::RGB32I:
            return 12;

        case TextureFormat::RGBA32F:
        case TextureFormat::RGBA32UI:
        case TextureFormat::RGBA32I:
            return 16;

        // 压缩格式：EAC/ETC2/DXT 的单块字节数，ASTC 固定为 16 字节
        case TextureFormat::EAC_R11:
        case TextureFormat::EAC_R11_SIGNED:
        case TextureFormat::ETC2_RGB8:
        case TextureFormat::ETC2_SRGB8:
        case TextureFormat::ETC2_RGB8_A1:
        case TextureFormat::ETC2_SRGB8_A1:
        case TextureFormat::DXT1_RGB:
        case TextureFormat::DXT1_RGBA:
        case TextureFormat::DXT1_SRGB:
        case TextureFormat::DXT1_SRGBA:
        case TextureFormat::RED_RGTC1:
        case TextureFormat::SIGNED_RED_RGTC1:
            return 8;

        case TextureFormat::EAC_RG11:
        case TextureFormat::EAC_RG11_SIGNED:
        case TextureFormat::ETC2_EAC_RGBA8:
        case TextureFormat::ETC2_EAC_SRGBA8:
        case TextureFormat::DXT3_RGBA:
        case TextureFormat::DXT3_SRGBA:
        case TextureFormat::DXT5_RGBA:
        case TextureFormat::DXT5_SRGBA:
        case TextureFormat::RED_GREEN_RGTC2:
        case TextureFormat::SIGNED_RED_GREEN_RGTC2:
        case TextureFormat::RGB_BPTC_SIGNED_FLOAT:
        case TextureFormat::RGB_BPTC_UNSIGNED_FLOAT:
        case TextureFormat::RGBA_BPTC_UNORM:
        case TextureFormat::SRGB_ALPHA_BPTC_UNORM:
        case TextureFormat::RGBA_ASTC_4x4:
        case TextureFormat::RGBA_ASTC_5x4:
        case TextureFormat::RGBA_ASTC_5x5:
        case TextureFormat::RGBA_ASTC_6x5:
        case TextureFormat::RGBA_ASTC_6x6:
        case TextureFormat::RGBA_ASTC_8x5:
        case TextureFormat::RGBA_ASTC_8x6:
        case TextureFormat::RGBA_ASTC_8x8:
        case TextureFormat::RGBA_ASTC_10x5:
        case TextureFormat::RGBA_ASTC_10x6:
        case TextureFormat::RGBA_ASTC_10x8:
        case TextureFormat::RGBA_ASTC_10x10:
        case TextureFormat::RGBA_ASTC_12x10:
        case TextureFormat::RGBA_ASTC_12x12:
        case TextureFormat::SRGB8_ALPHA8_ASTC_4x4:
        case TextureFormat::SRGB8_ALPHA8_ASTC_5x4:
        case TextureFormat::SRGB8_ALPHA8_ASTC_5x5:
        case TextureFormat::SRGB8_ALPHA8_ASTC_6x5:
        case TextureFormat::SRGB8_ALPHA8_ASTC_6x6:
        case TextureFormat::SRGB8_ALPHA8_ASTC_8x5:
        case TextureFormat::SRGB8_ALPHA8_ASTC_8x6:
        case TextureFormat::SRGB8_ALPHA8_ASTC_8x8:
        case TextureFormat::SRGB8_ALPHA8_ASTC_10x5:
        case TextureFormat::SRGB8_ALPHA8_ASTC_10x6:
        case TextureFormat::SRGB8_ALPHA8_ASTC_10x8:
        case TextureFormat::SRGB8_ALPHA8_ASTC_10x10:
        case TextureFormat::SRGB8_ALPHA8_ASTC_12x10:
        case TextureFormat::SRGB8_ALPHA8_ASTC_12x12:
            return 16;

        // 历史占位值，无对应存储格式
        case TextureFormat::UNUSED:
            return 0;
    }
    return 0;
}

VkCompareOp GetCompareOp(SamplerCompareFunc func) {
    switch (func) {
        CASE_FROM_TO(SamplerCompareFunc::Le, VK_COMPARE_OP_LESS_OR_EQUAL);
        CASE_FROM_TO(SamplerCompareFunc::Ge, VK_COMPARE_OP_GREATER_OR_EQUAL);
        CASE_FROM_TO(SamplerCompareFunc::L, VK_COMPARE_OP_LESS);
        CASE_FROM_TO(SamplerCompareFunc::G, VK_COMPARE_OP_GREATER);
        CASE_FROM_TO(SamplerCompareFunc::E, VK_COMPARE_OP_EQUAL);
        CASE_FROM_TO(SamplerCompareFunc::Ne, VK_COMPARE_OP_NOT_EQUAL);
        CASE_FROM_TO(SamplerCompareFunc::A, VK_COMPARE_OP_ALWAYS);
        CASE_FROM_TO(SamplerCompareFunc::N, VK_COMPARE_OP_NEVER);
    }
    return VK_COMPARE_OP_NEVER;
}

VkStencilOp GetStencilOp(StencilOperation op) {
    switch (op) {
        CASE_FROM_TO(StencilOperation::Keep, VK_STENCIL_OP_KEEP);
        CASE_FROM_TO(StencilOperation::Zero, VK_STENCIL_OP_ZERO);
        CASE_FROM_TO(StencilOperation::Replace, VK_STENCIL_OP_REPLACE);
        CASE_FROM_TO(StencilOperation::Incr, VK_STENCIL_OP_INCREMENT_AND_CLAMP);
        CASE_FROM_TO(StencilOperation::IncrWrap, VK_STENCIL_OP_INCREMENT_AND_WRAP);
        CASE_FROM_TO(StencilOperation::Decr, VK_STENCIL_OP_DECREMENT_AND_CLAMP);
        CASE_FROM_TO(StencilOperation::DecrWrap, VK_STENCIL_OP_DECREMENT_AND_WRAP);
        CASE_FROM_TO(StencilOperation::Invert, VK_STENCIL_OP_INVERT);
    }
    return VK_STENCIL_OP_KEEP;
}

VkBlendFactor GetBlendFactor(BlendFunction mode) {
    switch (mode) {
        CASE_FROM_TO(BlendFunction::Zero, VK_BLEND_FACTOR_ZERO);
        CASE_FROM_TO(BlendFunction::One, VK_BLEND_FACTOR_ONE);
        CASE_FROM_TO(BlendFunction::SrcColor, VK_BLEND_FACTOR_SRC_COLOR);
        CASE_FROM_TO(BlendFunction::OneMinusSrcColor, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR);
        CASE_FROM_TO(BlendFunction::DstColor, VK_BLEND_FACTOR_DST_COLOR);
        CASE_FROM_TO(BlendFunction::OneMinusDstColor, VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR);
        CASE_FROM_TO(BlendFunction::SrcAlpha, VK_BLEND_FACTOR_SRC_ALPHA);
        CASE_FROM_TO(BlendFunction::OneMinusSrcAlpha, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
        CASE_FROM_TO(BlendFunction::DstAlpha, VK_BLEND_FACTOR_DST_ALPHA);
        CASE_FROM_TO(BlendFunction::OneMinusDstAlpha, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA);
        CASE_FROM_TO(BlendFunction::SrcAlphaSaturate, VK_BLEND_FACTOR_SRC_ALPHA_SATURATE);
    }
    return VK_BLEND_FACTOR_ZERO;
}

VkCullModeFlags GetCullMode(CullingMode mode) {
    switch (mode) {
        CASE_FROM_TO(CullingMode::None, VK_CULL_MODE_NONE);
        CASE_FROM_TO(CullingMode::Front, VK_CULL_MODE_FRONT_BIT);
        CASE_FROM_TO(CullingMode::Back, VK_CULL_MODE_BACK_BIT);
        CASE_FROM_TO(CullingMode::FrontAndBack, VK_CULL_MODE_FRONT_AND_BACK);
    }
    return VK_CULL_MODE_NONE;
}

VkFrontFace GetFrontFace(bool inverseFrontFaces) {
    return inverseFrontFaces ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

PixelDataType GetComponentType(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R8_SNORM:
        case VK_FORMAT_R8_USCALED:
        case VK_FORMAT_R8_SSCALED:
        case VK_FORMAT_R8_UINT:
            return PixelDataType::UBYTE;
        CASE_FROM_TO(VK_FORMAT_R8_SINT, PixelDataType::BYTE);
        case VK_FORMAT_R8_SRGB:
        case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_R8G8_SNORM:
        case VK_FORMAT_R8G8_USCALED:
        case VK_FORMAT_R8G8_SSCALED:
        case VK_FORMAT_R8G8_UINT:
            return PixelDataType::UBYTE;
        CASE_FROM_TO(VK_FORMAT_R8G8_SINT, PixelDataType::BYTE);
        case VK_FORMAT_R8G8_SRGB:
        case VK_FORMAT_R8G8B8_UNORM:
        case VK_FORMAT_R8G8B8_SNORM:
        case VK_FORMAT_R8G8B8_USCALED:
        case VK_FORMAT_R8G8B8_SSCALED:
        case VK_FORMAT_R8G8B8_UINT:
            return PixelDataType::UBYTE;
        CASE_FROM_TO(VK_FORMAT_R8G8B8_SINT, PixelDataType::BYTE);
        case VK_FORMAT_R8G8B8_SRGB:
        case VK_FORMAT_B8G8R8_UNORM:
            return PixelDataType::UBYTE;
        CASE_FROM_TO(VK_FORMAT_B8G8R8_SNORM, PixelDataType::BYTE);
        case VK_FORMAT_B8G8R8_USCALED:
        case VK_FORMAT_B8G8R8_SSCALED:
        case VK_FORMAT_B8G8R8_UINT:
            return PixelDataType::UBYTE;
        CASE_FROM_TO(VK_FORMAT_B8G8R8_SINT, PixelDataType::BYTE);
        case VK_FORMAT_B8G8R8_SRGB:
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SNORM:
        case VK_FORMAT_R8G8B8A8_USCALED:
        case VK_FORMAT_R8G8B8A8_SSCALED:
        case VK_FORMAT_R8G8B8A8_UINT:
            return PixelDataType::UBYTE;
        CASE_FROM_TO(VK_FORMAT_R8G8B8A8_SINT, PixelDataType::BYTE);
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SNORM:
        case VK_FORMAT_B8G8R8A8_USCALED:
        case VK_FORMAT_B8G8R8A8_SSCALED:
        case VK_FORMAT_B8G8R8A8_UINT:
            return PixelDataType::UBYTE;
        CASE_FROM_TO(VK_FORMAT_B8G8R8A8_SINT, PixelDataType::BYTE);
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
        case VK_FORMAT_A8B8G8R8_SNORM_PACK32:
        case VK_FORMAT_A8B8G8R8_USCALED_PACK32:
        case VK_FORMAT_A8B8G8R8_SSCALED_PACK32:
        case VK_FORMAT_A8B8G8R8_UINT_PACK32:
            return PixelDataType::UBYTE;
        CASE_FROM_TO(VK_FORMAT_A8B8G8R8_SINT_PACK32, PixelDataType::BYTE);
        CASE_FROM_TO(VK_FORMAT_A8B8G8R8_SRGB_PACK32, PixelDataType::UBYTE);
        case VK_FORMAT_R16_UNORM:
        case VK_FORMAT_R16_SNORM:
        case VK_FORMAT_R16_USCALED:
        case VK_FORMAT_R16_SSCALED:
        case VK_FORMAT_R16_UINT:
            return PixelDataType::USHORT;
        CASE_FROM_TO(VK_FORMAT_R16_SINT, PixelDataType::SHORT);
        CASE_FROM_TO(VK_FORMAT_R16_SFLOAT, PixelDataType::HALF);
        case VK_FORMAT_R16G16_UNORM:
        case VK_FORMAT_R16G16_SNORM:
        case VK_FORMAT_R16G16_USCALED:
        case VK_FORMAT_R16G16_SSCALED:
        case VK_FORMAT_R16G16_UINT:
            return PixelDataType::USHORT;
        CASE_FROM_TO(VK_FORMAT_R16G16_SINT, PixelDataType::SHORT);
        CASE_FROM_TO(VK_FORMAT_R16G16_SFLOAT, PixelDataType::HALF);
        case VK_FORMAT_R16G16B16_UNORM:
        case VK_FORMAT_R16G16B16_SNORM:
        case VK_FORMAT_R16G16B16_USCALED:
        case VK_FORMAT_R16G16B16_SSCALED:
        case VK_FORMAT_R16G16B16_UINT:
            return PixelDataType::USHORT;
        CASE_FROM_TO(VK_FORMAT_R16G16B16_SINT, PixelDataType::SHORT);
        CASE_FROM_TO(VK_FORMAT_R16G16B16_SFLOAT, PixelDataType::HALF);
        case VK_FORMAT_R16G16B16A16_UNORM:
        case VK_FORMAT_R16G16B16A16_SNORM:
        case VK_FORMAT_R16G16B16A16_USCALED:
        case VK_FORMAT_R16G16B16A16_SSCALED:
        case VK_FORMAT_R16G16B16A16_UINT:
            return PixelDataType::USHORT;
        CASE_FROM_TO(VK_FORMAT_R16G16B16A16_SINT, PixelDataType::SHORT);
        CASE_FROM_TO(VK_FORMAT_R16G16B16A16_SFLOAT, PixelDataType::HALF);
        CASE_FROM_TO(VK_FORMAT_R32_UINT, PixelDataType::UINT);
        CASE_FROM_TO(VK_FORMAT_R32_SINT, PixelDataType::INT);
        CASE_FROM_TO(VK_FORMAT_R32_SFLOAT, PixelDataType::FLOAT);
        CASE_FROM_TO(VK_FORMAT_R32G32_UINT, PixelDataType::UINT);
        CASE_FROM_TO(VK_FORMAT_R32G32_SINT, PixelDataType::INT);
        CASE_FROM_TO(VK_FORMAT_R32G32_SFLOAT, PixelDataType::FLOAT);
        CASE_FROM_TO(VK_FORMAT_R32G32B32_UINT, PixelDataType::UINT);
        CASE_FROM_TO(VK_FORMAT_R32G32B32_SINT, PixelDataType::INT);
        CASE_FROM_TO(VK_FORMAT_R32G32B32_SFLOAT, PixelDataType::FLOAT);
        CASE_FROM_TO(VK_FORMAT_R32G32B32A32_UINT, PixelDataType::UINT);
        CASE_FROM_TO(VK_FORMAT_R32G32B32A32_SINT, PixelDataType::INT);
        CASE_FROM_TO(VK_FORMAT_R32G32B32A32_SFLOAT, PixelDataType::FLOAT);
        CASE_FROM_TO(VK_FORMAT_D16_UNORM, PixelDataType::USHORT);
        CASE_FROM_TO(VK_FORMAT_D32_SFLOAT, PixelDataType::FLOAT);
        CASE_FROM_TO(VK_FORMAT_X8_D24_UNORM_PACK32, PixelDataType::UINT);
        CASE_FROM_TO(VK_FORMAT_B10G11R11_UFLOAT_PACK32, PixelDataType::UINT_10F_11F_11F_REV);
        // 合并的深度/模板格式按主（深度）分量定类型，模板分量的覆盖在回读时另行处理
        CASE_FROM_TO(VK_FORMAT_D24_UNORM_S8_UINT, PixelDataType::UINT);
        CASE_FROM_TO(VK_FORMAT_D32_SFLOAT_S8_UINT, PixelDataType::FLOAT);
        default:
            assert_invariant(false && "Unknown data type, conversion is not supported.");
            return {};
    }
}

uint32_t GetComponentCount(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R8_SNORM:
        case VK_FORMAT_R8_USCALED:
        case VK_FORMAT_R8_SSCALED:
        case VK_FORMAT_R8_UINT:
        case VK_FORMAT_R8_SINT:
        case VK_FORMAT_R8_SRGB:
        case VK_FORMAT_R16_UNORM:
        case VK_FORMAT_R16_SNORM:
        case VK_FORMAT_R16_USCALED:
        case VK_FORMAT_R16_SSCALED:
        case VK_FORMAT_R16_UINT:
        case VK_FORMAT_R16_SINT:
        case VK_FORMAT_R16_SFLOAT:
        case VK_FORMAT_R32_UINT:
        case VK_FORMAT_R32_SINT:
        case VK_FORMAT_R32_SFLOAT:
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
            return 1;

        case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_R8G8_SNORM:
        case VK_FORMAT_R8G8_USCALED:
        case VK_FORMAT_R8G8_SSCALED:
        case VK_FORMAT_R8G8_UINT:
        case VK_FORMAT_R8G8_SINT:
        case VK_FORMAT_R8G8_SRGB:
        case VK_FORMAT_R16G16_UNORM:
        case VK_FORMAT_R16G16_SNORM:
        case VK_FORMAT_R16G16_USCALED:
        case VK_FORMAT_R16G16_SSCALED:
        case VK_FORMAT_R16G16_UINT:
        case VK_FORMAT_R16G16_SINT:
        case VK_FORMAT_R16G16_SFLOAT:
        case VK_FORMAT_R32G32_UINT:
        case VK_FORMAT_R32G32_SINT:
        case VK_FORMAT_R32G32_SFLOAT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return 2;

        case VK_FORMAT_R8G8B8_UNORM:
        case VK_FORMAT_R8G8B8_SNORM:
        case VK_FORMAT_R8G8B8_USCALED:
        case VK_FORMAT_R8G8B8_SSCALED:
        case VK_FORMAT_R8G8B8_UINT:
        case VK_FORMAT_R8G8B8_SINT:
        case VK_FORMAT_R8G8B8_SRGB:
        case VK_FORMAT_B8G8R8_UNORM:
        case VK_FORMAT_B8G8R8_SNORM:
        case VK_FORMAT_B8G8R8_USCALED:
        case VK_FORMAT_B8G8R8_SSCALED:
        case VK_FORMAT_B8G8R8_UINT:
        case VK_FORMAT_B8G8R8_SINT:
        case VK_FORMAT_B8G8R8_SRGB:
        case VK_FORMAT_R16G16B16_UNORM:
        case VK_FORMAT_R16G16B16_SNORM:
        case VK_FORMAT_R16G16B16_USCALED:
        case VK_FORMAT_R16G16B16_SSCALED:
        case VK_FORMAT_R16G16B16_UINT:
        case VK_FORMAT_R16G16B16_SINT:
        case VK_FORMAT_R16G16B16_SFLOAT:
        case VK_FORMAT_R32G32B32_UINT:
        case VK_FORMAT_R32G32B32_SINT:
        case VK_FORMAT_R32G32B32_SFLOAT:
        case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
        case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
            return 3;

        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SNORM:
        case VK_FORMAT_R8G8B8A8_USCALED:
        case VK_FORMAT_R8G8B8A8_SSCALED:
        case VK_FORMAT_R8G8B8A8_UINT:
        case VK_FORMAT_R8G8B8A8_SINT:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SNORM:
        case VK_FORMAT_B8G8R8A8_USCALED:
        case VK_FORMAT_B8G8R8A8_SSCALED:
        case VK_FORMAT_B8G8R8A8_UINT:
        case VK_FORMAT_B8G8R8A8_SINT:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
        case VK_FORMAT_A8B8G8R8_SNORM_PACK32:
        case VK_FORMAT_A8B8G8R8_USCALED_PACK32:
        case VK_FORMAT_A8B8G8R8_SSCALED_PACK32:
        case VK_FORMAT_A8B8G8R8_UINT_PACK32:
        case VK_FORMAT_A8B8G8R8_SINT_PACK32:
        case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        case VK_FORMAT_R16G16B16A16_UNORM:
        case VK_FORMAT_R16G16B16A16_SNORM:
        case VK_FORMAT_R16G16B16A16_USCALED:
        case VK_FORMAT_R16G16B16A16_SSCALED:
        case VK_FORMAT_R16G16B16A16_UINT:
        case VK_FORMAT_R16G16B16A16_SINT:
        case VK_FORMAT_R16G16B16A16_SFLOAT:
        case VK_FORMAT_R32G32B32A32_UINT:
        case VK_FORMAT_R32G32B32A32_SINT:
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return 4;
        default:
            assert_invariant(false && "Unknown data type, conversion is not supported.");
            return {};
    }
}

VkComponentMapping GetSwizzleMap(TextureSwizzle const swizzle[4]) {
    VkComponentMapping  map;
    VkComponentSwizzle *dst = &map.r;
    for (int i = 0; i < 4; ++i, ++dst) {
        // 通道恰好落在自身位置时用 IDENTITY：更可能与其他状态共享，且部分驱动上更省。
        // 各分支是「赋值」而非单一返回值，CASE_FROM_TO 只适用于返回值，故此处保留展开写法
        switch (swizzle[i]) {
            case TextureSwizzle::SubstituteZero:
                *dst = VK_COMPONENT_SWIZZLE_ZERO;
                break;
            case TextureSwizzle::SubstituteOne:
                *dst = VK_COMPONENT_SWIZZLE_ONE;
                break;
            case TextureSwizzle::Channel0:
                *dst = i == 0 ? VK_COMPONENT_SWIZZLE_IDENTITY : VK_COMPONENT_SWIZZLE_R;
                break;
            case TextureSwizzle::Channel1:
                *dst = i == 1 ? VK_COMPONENT_SWIZZLE_IDENTITY : VK_COMPONENT_SWIZZLE_G;
                break;
            case TextureSwizzle::Channel2:
                *dst = i == 2 ? VK_COMPONENT_SWIZZLE_IDENTITY : VK_COMPONENT_SWIZZLE_B;
                break;
            case TextureSwizzle::Channel3:
                *dst = i == 3 ? VK_COMPONENT_SWIZZLE_IDENTITY : VK_COMPONENT_SWIZZLE_A;
                break;
        }
    }
    return map;
}

VkFilter GetFilter(SamplerMinFilter filter) {
    switch (filter) {
        CASE_FROM_TO(SamplerMinFilter::Nearest, VK_FILTER_NEAREST);
        CASE_FROM_TO(SamplerMinFilter::Linear, VK_FILTER_LINEAR);
        CASE_FROM_TO(SamplerMinFilter::NearestMipmapNearest, VK_FILTER_NEAREST);
        CASE_FROM_TO(SamplerMinFilter::LinearMipmapNearest, VK_FILTER_LINEAR);
        CASE_FROM_TO(SamplerMinFilter::NearestMipmapLinear, VK_FILTER_NEAREST);
        CASE_FROM_TO(SamplerMinFilter::LinearMipmapLinear, VK_FILTER_LINEAR);
    }
    return VK_FILTER_NEAREST;
}

VkFilter GetFilter(SamplerMagFilter filter) {
    switch (filter) {
        CASE_FROM_TO(SamplerMagFilter::Nearest, VK_FILTER_NEAREST);
        CASE_FROM_TO(SamplerMagFilter::Linear, VK_FILTER_LINEAR);
    }
    return VK_FILTER_NEAREST;
}

VkSamplerMipmapMode GetMipmapMode(SamplerMinFilter filter) {
    switch (filter) {
        CASE_FROM_TO(SamplerMinFilter::Nearest, VK_SAMPLER_MIPMAP_MODE_NEAREST);
        CASE_FROM_TO(SamplerMinFilter::Linear, VK_SAMPLER_MIPMAP_MODE_NEAREST);
        CASE_FROM_TO(SamplerMinFilter::NearestMipmapNearest, VK_SAMPLER_MIPMAP_MODE_NEAREST);
        CASE_FROM_TO(SamplerMinFilter::LinearMipmapNearest, VK_SAMPLER_MIPMAP_MODE_NEAREST);
        CASE_FROM_TO(SamplerMinFilter::NearestMipmapLinear, VK_SAMPLER_MIPMAP_MODE_LINEAR);
        CASE_FROM_TO(SamplerMinFilter::LinearMipmapLinear, VK_SAMPLER_MIPMAP_MODE_LINEAR);
    }
    return VK_SAMPLER_MIPMAP_MODE_NEAREST;
}

VkSamplerAddressMode GetWrapMode(SamplerWrapMode mode) {
    switch (mode) {
        CASE_FROM_TO(SamplerWrapMode::Repeat, VK_SAMPLER_ADDRESS_MODE_REPEAT);
        CASE_FROM_TO(SamplerWrapMode::ClampToEdge, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        CASE_FROM_TO(SamplerWrapMode::MirroredRepeat, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT);
    }
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

VkBool32 GetCompareEnable(SamplerCompareMode mode) {
    return mode == SamplerCompareMode::None ? VK_FALSE : VK_TRUE;
}

float GetMaxLod(SamplerMinFilter filter) {
    switch (filter) {
        case SamplerMinFilter::Nearest:
        case SamplerMinFilter::Linear:
            // Vulkan 规范在「OpenGL 到 Vulkan 过滤模式映射」一节建议用 0.25 表示关闭 mipmap
            return 0.25f;
        case SamplerMinFilter::NearestMipmapNearest:
        case SamplerMinFilter::LinearMipmapNearest:
        case SamplerMinFilter::NearestMipmapLinear:
        case SamplerMinFilter::LinearMipmapLinear:
            return VK_LOD_CLAMP_NONE;
    }
    return 0.25f;
}

VkShaderStageFlags GetShaderStageFlags(ShaderStageFlags stageFlags) {
    VkShaderStageFlags flags = 0x0;
    if (static_cast<uint8_t>(stageFlags & ShaderStageFlags::VERTEX) != 0) flags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (static_cast<uint8_t>(stageFlags & ShaderStageFlags::FRAGMENT) != 0) flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    return flags;
}

VkSamplerYcbcrModelConversion GetYcbcrModelConversion(SamplerYcbcrModelConversion model) {
    switch (model) {
        CASE_FROM_TO(SamplerYcbcrModelConversion::RgbIdentity, VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY);
        CASE_FROM_TO(SamplerYcbcrModelConversion::YcbcrIdentity, VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_IDENTITY);
        CASE_FROM_TO(SamplerYcbcrModelConversion::Ycbcr709, VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709);
        CASE_FROM_TO(SamplerYcbcrModelConversion::Ycbcr601, VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601);
        CASE_FROM_TO(SamplerYcbcrModelConversion::Ycbcr2020, VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_2020);
    }
    assert_invariant(false && "Unknown data type, conversion is not supported.");
    return VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY;
}

VkSamplerYcbcrRange GetYcbcrRange(SamplerYcbcrRange range) {
    switch (range) {
        CASE_FROM_TO(SamplerYcbcrRange::ItuFull, VK_SAMPLER_YCBCR_RANGE_ITU_FULL);
        CASE_FROM_TO(SamplerYcbcrRange::ItuNarrow, VK_SAMPLER_YCBCR_RANGE_ITU_NARROW);
    }
    assert_invariant(false && "Unknown data type, conversion is not supported.");
    return VK_SAMPLER_YCBCR_RANGE_ITU_FULL;
}

VkChromaLocation GetChromaLocation(ChromaLocation loc) {
    switch (loc) {
        CASE_FROM_TO(ChromaLocation::CositedEven, VK_CHROMA_LOCATION_COSITED_EVEN);
        CASE_FROM_TO(ChromaLocation::Midpoint, VK_CHROMA_LOCATION_MIDPOINT);
    }
    assert_invariant(false && "Unknown data type, conversion is not supported.");
    return VK_CHROMA_LOCATION_COSITED_EVEN;
}

SamplerYcbcrModelConversion GetYcbcrModelConversionFilament(VkSamplerYcbcrModelConversion model) {
    switch (model) {
        CASE_FROM_TO(VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY, SamplerYcbcrModelConversion::RgbIdentity);
        CASE_FROM_TO(VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_IDENTITY, SamplerYcbcrModelConversion::YcbcrIdentity);
        CASE_FROM_TO(VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709, SamplerYcbcrModelConversion::Ycbcr709);
        CASE_FROM_TO(VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601, SamplerYcbcrModelConversion::Ycbcr601);
        CASE_FROM_TO(VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_2020, SamplerYcbcrModelConversion::Ycbcr2020);
        default:
            assert_invariant(false && "Unknown data type, conversion is not supported.");
            return SamplerYcbcrModelConversion::RgbIdentity;
    }
}

SamplerYcbcrRange GetYcbcrRangeFilament(VkSamplerYcbcrRange range) {
    switch (range) {
        CASE_FROM_TO(VK_SAMPLER_YCBCR_RANGE_ITU_FULL, SamplerYcbcrRange::ItuFull);
        CASE_FROM_TO(VK_SAMPLER_YCBCR_RANGE_ITU_NARROW, SamplerYcbcrRange::ItuNarrow);
        default:
            assert_invariant(false && "Unknown data type, conversion is not supported.");
            return SamplerYcbcrRange::ItuFull;
    }
}

ChromaLocation GetChromaLocationFilament(VkChromaLocation loc) {
    switch (loc) {
        CASE_FROM_TO(VK_CHROMA_LOCATION_COSITED_EVEN, ChromaLocation::CositedEven);
        CASE_FROM_TO(VK_CHROMA_LOCATION_MIDPOINT, ChromaLocation::Midpoint);
        default:
            assert_invariant(false && "Unknown data type, conversion is not supported.");
            return ChromaLocation::CositedEven;
    }
}

TextureSwizzle GetSwizzleFilament(VkComponentSwizzle c, uint8_t rgbaIndex) {
    switch (c) {
        CASE_FROM_TO(VK_COMPONENT_SWIZZLE_ZERO, TextureSwizzle::SubstituteZero);
        CASE_FROM_TO(VK_COMPONENT_SWIZZLE_ONE, TextureSwizzle::SubstituteOne);
        case VK_COMPONENT_SWIZZLE_IDENTITY:
            // 同一格式下 IDENTITY 表示「取本位分量」，故按索引折算回对应通道
            return static_cast<TextureSwizzle>(static_cast<uint8_t>(TextureSwizzle::Channel0) + rgbaIndex);
        CASE_FROM_TO(VK_COMPONENT_SWIZZLE_R, TextureSwizzle::Channel0);
        CASE_FROM_TO(VK_COMPONENT_SWIZZLE_G, TextureSwizzle::Channel1);
        CASE_FROM_TO(VK_COMPONENT_SWIZZLE_B, TextureSwizzle::Channel2);
        CASE_FROM_TO(VK_COMPONENT_SWIZZLE_A, TextureSwizzle::Channel3);
        default:
            assert_invariant(false && "Unknown data type, conversion is not supported.");
            return TextureSwizzle::SubstituteZero;
    }
}

}  // namespace VK_UTILS

END_NS_BACKEND
