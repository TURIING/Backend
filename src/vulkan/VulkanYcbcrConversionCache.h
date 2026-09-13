#pragma once

#include "Backend/DriverDefine.h"

#include "Utils/Hash.h"
#include "Utils/Macro.h"
#include "Utils/Utils.h"

#include <cstdint>
#include <unordered_map>

#include "vulkan/utils/Definitions.h"

BEGIN_NS_BACKEND

// VkSamplerYcbcrConversion 的只增缓存
class VulkanYcbcrConversionCache {
public:
    struct Params {
        VK_UTILS::SamplerYcbcrConversion conversion     = {};  // 4
        VkFormat                         format         = VK_FORMAT_UNDEFINED;
        uint64_t                         externalFormat = 0;  // 8
    };

    static_assert(sizeof(Params) == 16);

    explicit VulkanYcbcrConversionCache(VkDevice device);

    NODISCARD VkSamplerYcbcrConversion GetConversion(Params params);

    void Terminate() noexcept;

private:
    struct ConversionEqualTo {
        bool operator()(Params lhs, Params rhs) const noexcept {
            VK_UTILS::SamplerYcbcrConversion::EqualTo equal;
            return equal(lhs.conversion, rhs.conversion) && lhs.externalFormat == rhs.externalFormat && lhs.format == rhs.format;
        }
    };
    using ConversionHashFn = NS_UTILS::hash::MurmurHashFn<Params>;

    VkDevice m_device;

    std::unordered_map<Params, VkSamplerYcbcrConversion, ConversionHashFn, ConversionEqualTo> m_cache;
};

END_NS_BACKEND
