#pragma once

#include "Backend/DriverDefine.h"
#include "Backend/Program.h"
#include "Backend/TargetBufferInfo.h"

#include "Utils/Hash.h"
#include "Utils/Macro.h"
#include "Utils/Utils.h"

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "vulkan/VkDef.h"
#include "vulkan/VulkanHandle.h"
#include "vulkan/resource/ResourceManager.h"

BEGIN_NS_BACKEND

// VkDescriptorSetLayout 的只增缓存。
//
// 缓存键是布局的位掩码描述而非后端布局对象：同一组绑定在不同 program 里会给出等价的
// 掩码，据此可跨 program 复用布局。
class VulkanDescriptorSetLayoutCache : public NS_UTILS::Ref {
public:
    VulkanDescriptorSetLayoutCache(VkDevice device, const ResourceManagerPtr& resourceManager);
    ~VulkanDescriptorSetLayoutCache();

    VulkanDescriptorSetLayoutCache(VulkanDescriptorSetLayoutCache const&)            = delete;
    VulkanDescriptorSetLayoutCache& operator=(VulkanDescriptorSetLayoutCache const&) = delete;

    // 须在 VkDevice 销毁前调用；语义幂等
    void Terminate() noexcept;

    NODISCARD VulkanDescriptorSetLayoutPtr CreateLayout(Handle<HwDescriptorSetLayout> handle, DescriptorSetLayout&& info);

    // 供外部采样器路径使用：同一布局可带不可变采样器再取一份
    NODISCARD VkDescriptorSetLayout TransVulkanLayoutToVkImageLayout(VulkanDescriptorSetLayout::Bitmask const& bitmasks, VK_UTILS::SamplerBitmask externalSamplers,
                                                std::vector<std::pair<uint64_t, VkSampler>> immutableSamplers = {});

private:
    struct LayoutKey {
        VulkanDescriptorSetLayout::Bitmask bitmask = {};  // 用位掩码而非绑定的具体字段描述布局
        uint64_t immutableSamplerHash = 0;                // 不可变采样器个数不定，整体哈希成 64 位参与比较
    };
    static_assert(sizeof(LayoutKey) == 48);

    using LayoutKeyHashFn = NS_UTILS::hash::MurmurHashFn<LayoutKey>;

    struct LayoutKeyEqual {
        bool operator()(LayoutKey const& k1, LayoutKey const& k2) const {
            return k1.bitmask == k2.bitmask && k1.immutableSamplerHash == k2.immutableSamplerHash;
        }
    };

    using LayoutMap = std::unordered_map<LayoutKey, VkDescriptorSetLayout, LayoutKeyHashFn, LayoutKeyEqual>;

    VkDevice           m_device;
    ResourceManagerPtr m_resourceManager;
    LayoutMap          m_vkLayouts;
};

DECLARE_SHARE_PTR_CLASS(VulkanDescriptorSetLayoutCache);

END_NS_BACKEND
