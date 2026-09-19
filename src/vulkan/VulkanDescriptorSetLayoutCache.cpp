#include "vulkan/VulkanDescriptorSetLayoutCache.h"

#include "Utils/Panic.h"

#include "vulkan/VulkanHandle.h"

#include <algorithm>
#include <cstdint>

BEGIN_NS_BACKEND

namespace {

using BitmaskGroup = VulkanDescriptorSetLayout::Bitmask;

// 把「绑定下标 + 阶段」折叠成一条 Vulkan 绑定：同一下标若两个阶段都声明，只出一条并置两个
// stageFlags；已在顶点半区处理过的片元位由 alreadySeen 跳过
template <typename Bitmask>
uint32_t AppendBindings(VkDescriptorSetLayoutBinding* toBind, VkDescriptorType type, Bitmask const& mask) {
    uint32_t count = 0;
    Bitmask  alreadySeen;
    mask.ForEachSetBit([&](size_t index) {
        VkShaderStageFlags stages  = 0;
        uint32_t           binding = 0;
        if (index < VK_UTILS::GetFragmentStageShift<Bitmask>()) {
            binding = static_cast<uint32_t>(index);
            stages |= VK_SHADER_STAGE_VERTEX_BIT;
            auto const fragIndex = index + VK_UTILS::GetFragmentStageShift<Bitmask>();
            if (mask.Test(fragIndex)) {
                stages |= VK_SHADER_STAGE_FRAGMENT_BIT;
                alreadySeen.Set(fragIndex);
            }
        } else if (!alreadySeen.Test(index)) {
            binding = static_cast<uint32_t>(index - VK_UTILS::GetFragmentStageShift<Bitmask>());
            stages |= VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        if (stages) {
            toBind[count++] = {
                .binding        = binding,
                .descriptorType = type,
                .descriptorCount = 1,
                .stageFlags     = stages,
            };
        }
    });
    return count;
}

uint32_t AppendSamplerBindings(VkDescriptorSetLayoutBinding* toBind, VK_UTILS::SamplerBitmask const& mask,
                               VK_UTILS::SamplerBitmask const& external,
                               std::vector<std::pair<uint64_t, VkSampler>> const& immutableSamplers) {
    using Bitmask = VK_UTILS::SamplerBitmask;
    uint32_t count          = 0;
    Bitmask  alreadySeen;
    uint8_t  immutableIndex = 0;
    size_t const immutableSamplerCount = immutableSamplers.size();
    mask.ForEachSetBit([&](size_t index) {
        VkShaderStageFlags stages  = 0;
        uint32_t           binding = 0;
        if (index < VK_UTILS::GetFragmentStageShift<Bitmask>()) {
            binding = static_cast<uint32_t>(index);
            stages |= VK_SHADER_STAGE_VERTEX_BIT;
            auto const fragIndex = index + VK_UTILS::GetFragmentStageShift<Bitmask>();
            if (mask.Test(fragIndex)) {
                stages |= VK_SHADER_STAGE_FRAGMENT_BIT;
                alreadySeen.Set(fragIndex);
            }
        } else if (!alreadySeen.Test(index)) {
            binding = static_cast<uint32_t>(index - VK_UTILS::GetFragmentStageShift<Bitmask>());
            stages |= VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        if (stages) {
            toBind[count++] = {
                .binding           = binding,
                .descriptorType    = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount   = 1,
                .stageFlags        = stages,
                // 不可变采样器只对「外部采样器且还有剩余项」的绑定有效
                .pImmutableSamplers = external[index] && immutableSamplerCount > immutableIndex
                                              ? &immutableSamplers[immutableIndex++].second
                                              : nullptr,
            };
        }
    });
    return count;
}

uint64_t ComputeImmutableSamplerHash(std::vector<std::pair<uint64_t, VkSampler>> const& samplers) {
    size_t const size = samplers.size();
    if (size == 0) {
        return 0;
    }
    // 每个元素 64 位采样器 + 64 位转换句柄 = 4 个字
    return NS_UTILS::hash::Murmur3(reinterpret_cast<uint32_t const*>(samplers.data()), size * 4, 0);
}

}  // namespace

VulkanDescriptorSetLayoutCache::VulkanDescriptorSetLayoutCache(VkDevice device, const ResourceManagerPtr& resourceManager)
    : m_device(device), m_resourceManager(resourceManager) {}

VulkanDescriptorSetLayoutCache::~VulkanDescriptorSetLayoutCache() = default;

void VulkanDescriptorSetLayoutCache::Terminate() noexcept {
    for (auto& entry : m_vkLayouts) {
        vkDestroyDescriptorSetLayout(m_device, entry.second, kVkAlloc);
    }
    m_vkLayouts.clear();
}

VkDescriptorSetLayout VulkanDescriptorSetLayoutCache::TransVulkanLayoutToVkImageLayout(VulkanDescriptorSetLayout::Bitmask const& bitmasks,
                                                                 VK_UTILS::SamplerBitmask externalSamplers,
                                                                 std::vector<std::pair<uint64_t, VkSampler>> immutableSamplers) {
    LayoutKey key = {
        .bitmask              = bitmasks,
        .immutableSamplerHash = ComputeImmutableSamplerHash(immutableSamplers),
    };
    if (auto const iter = m_vkLayouts.find(key); iter != m_vkLayouts.end()) {
        return iter->second;
    }

    VkDescriptorSetLayoutBinding toBind[VulkanDescriptorSetLayout::kMaxBindings];
    uint32_t                     count = 0;
    count += AppendBindings(&toBind[count], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, bitmasks.dynamicUbo);
    count += AppendBindings(&toBind[count], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, bitmasks.ubo);
    count += AppendSamplerBindings(&toBind[count], bitmasks.sampler, externalSamplers, immutableSamplers);
    count += AppendBindings(&toBind[count], VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, bitmasks.inputAttachment);

    FILAMENT_CHECK_POSTCONDITION(count != 0) << "Need at least one binding for descriptor set layout.";
    VkDescriptorSetLayoutCreateInfo dlinfo = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = count,
        .pBindings    = toBind,
    };
    VkDescriptorSetLayout vkLayout = VK_NULL_HANDLE;
    vkCreateDescriptorSetLayout(m_device, &dlinfo, kVkAlloc, &vkLayout);
    m_vkLayouts.emplace(key, vkLayout);
    return vkLayout;
}

VulkanDescriptorSetLayoutPtr VulkanDescriptorSetLayoutCache::CreateLayout(Handle<HwDescriptorSetLayout> handle, DescriptorSetLayout&& info) {
    BitmaskGroup maskGroup = VulkanDescriptorSetLayout::Bitmask::FromLayoutDescription(info);
    return m_resourceManager->Make<VulkanDescriptorSetLayout>(handle, std::move(info), TransVulkanLayoutToVkImageLayout(maskGroup, maskGroup.externalSampler));
}

END_NS_BACKEND
