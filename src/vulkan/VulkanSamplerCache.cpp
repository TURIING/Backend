#include "vulkan/VulkanSamplerCache.h"

#include "Utils/Panic.h"

#include "vulkan/VkDef.h"
#include "vulkan/utils/Conversion.h"

BEGIN_NS_BACKEND

VulkanSamplerCache::VulkanSamplerCache(VkDevice device) : m_device(device) {}

VkSampler VulkanSamplerCache::GetSampler(Params params) {
    if (auto iter = m_cache.find(params); iter != m_cache.end()) {
        return iter->second;
    }

    VkSamplerYcbcrConversionInfo ycbcrConversion = {
        .sType      = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO,
        .conversion = params.conversion,
    };

    auto const&         samplerParams = params.sampler;
    VkSamplerCreateInfo samplerInfo{
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext                   = params.conversion != VK_NULL_HANDLE ? &ycbcrConversion : nullptr,
        .magFilter               = VK_UTILS::GetFilter(samplerParams.filterMag),
        .minFilter               = VK_UTILS::GetFilter(samplerParams.filterMin),
        .mipmapMode              = VK_UTILS::GetMipmapMode(samplerParams.filterMin),
        .addressModeU            = VK_UTILS::GetWrapMode(samplerParams.wrapS),
        .addressModeV            = VK_UTILS::GetWrapMode(samplerParams.wrapT),
        .addressModeW            = VK_UTILS::GetWrapMode(samplerParams.wrapR),
        .anisotropyEnable        = samplerParams.anisotropyLog2 == 0 ? VK_FALSE : VK_TRUE,
        .maxAnisotropy           = static_cast<float>(1u << samplerParams.anisotropyLog2),
        .compareEnable           = VK_UTILS::GetCompareEnable(samplerParams.compareMode),
        .compareOp               = VK_UTILS::GetCompareOp(samplerParams.compareFunc),
        .minLod                  = 0.0f,
        .maxLod                  = VK_UTILS::GetMaxLod(samplerParams.filterMin),
        .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };

    VkSampler  sampler = VK_NULL_HANDLE;
    VkResult const result = vkCreateSampler(m_device, &samplerInfo, kVkAlloc, &sampler);
    FILAMENT_CHECK_POSTCONDITION(result == VK_SUCCESS) << "Unable to create sampler." << " error=" << static_cast<int32_t>(result);

    m_cache.insert({ params, sampler });
    return sampler;
}

void VulkanSamplerCache::Terminate() noexcept {
    for (auto pair : m_cache) {
        vkDestroySampler(m_device, pair.second, kVkAlloc);
    }
    m_cache.clear();
}

END_NS_BACKEND
