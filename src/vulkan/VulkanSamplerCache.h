#pragma once

#include "Backend/DriverDefine.h"

#include "Utils/Hash.h"
#include "Utils/Macro.h"
#include "Utils/Utils.h"

#include <cstdint>
#include <unordered_map>

BEGIN_NS_BACKEND

// VkSampler 的只增缓存：采样器一旦创建即在池内复用，没有淘汰路径
class VulkanSamplerCache : public NS_UTILS::Ref {
public:
    struct Params {
        SamplerParams           sampler    = {};
        uint32_t                padding    = 0;
        VkSamplerYcbcrConversion conversion = VK_NULL_HANDLE;
    };

    static_assert(sizeof(Params) == 16);

    explicit VulkanSamplerCache(VkDevice device);

    NODISCARD VkSampler GetSampler(Params params);

    void Terminate() noexcept;

private:
    struct SamplerEqualTo {
        bool operator()(Params lhs, Params rhs) const noexcept {
            SamplerParams::EqualTo equal;
            return equal(lhs.sampler, rhs.sampler) && lhs.conversion == rhs.conversion;
        }
    };
    using SamplerHashFn = NS_UTILS::hash::MurmurHashFn<Params>;

    VkDevice m_device;

    std::unordered_map<Params, VkSampler, SamplerHashFn, SamplerEqualTo> m_cache;
};

DECLARE_SHARE_PTR_CLASS(VulkanSamplerCache);

END_NS_BACKEND
