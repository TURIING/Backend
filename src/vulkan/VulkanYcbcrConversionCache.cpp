#include "vulkan/VulkanYcbcrConversionCache.h"

#include "Utils/Panic.h"

#include "vulkan/VkDef.h"
#include "vulkan/utils/Conversion.h"

BEGIN_NS_BACKEND

VulkanYcbcrConversionCache::VulkanYcbcrConversionCache(VkDevice device) : m_device(device) {}

VkSamplerYcbcrConversion VulkanYcbcrConversionCache::GetConversion(Params params) {
    if (auto iter = m_cache.find(params); iter != m_cache.end()) {
        return iter->second;
    }

    auto const& chroma = params.conversion;

    // 外部格式（AHardwareBuffer）路径按既定决策砍掉，externalFormat 仅作为缓存键的一部分保留
    TextureSwizzle const swizzleArray[] = { chroma.r, chroma.g, chroma.b, chroma.a };

    VkSamplerYcbcrConversionCreateInfo const conversionInfo{
        .sType         = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO,
        .format        = params.format,
        .ycbcrModel    = VK_UTILS::GetYcbcrModelConversion(chroma.ycbcrModel),
        .ycbcrRange    = VK_UTILS::GetYcbcrRange(chroma.ycbcrRange),
        .components    = VK_UTILS::GetSwizzleMap(swizzleArray),
        .xChromaOffset = VK_UTILS::GetChromaLocation(chroma.xChromaOffset),
        .yChromaOffset = VK_UTILS::GetChromaLocation(chroma.yChromaOffset),
        .chromaFilter  = VK_UTILS::GetFilter(chroma.chromaFilter),
    };

    VkSamplerYcbcrConversion conversion = VK_NULL_HANDLE;
    VkResult const            result     = vkCreateSamplerYcbcrConversion(m_device, &conversionInfo, kVkAlloc, &conversion);
    FILAMENT_CHECK_POSTCONDITION(result == VK_SUCCESS)
            << "Unable to create Ycbcr Conversion." << " error=" << static_cast<int32_t>(result);

    m_cache.insert({ params, conversion });
    return conversion;
}

void VulkanYcbcrConversionCache::Terminate() noexcept {
    for (auto& [params, conversion] : m_cache) {
        vkDestroySamplerYcbcrConversion(m_device, conversion, kVkAlloc);
    }
    m_cache.clear();
}

END_NS_BACKEND
