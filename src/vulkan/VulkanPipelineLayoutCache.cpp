#include "vulkan/VulkanPipelineLayoutCache.h"

#include "Utils/Log.h"
#include "Utils/Macro.h"

#include <cstdint>

BEGIN_NS_BACKEND

VkPipelineLayout VulkanPipelineLayoutCache::GetLayout(DescriptorSetLayoutArray const& descriptorSetLayouts, VulkanProgramPtr const& program) {
    PipelineLayoutKey key         = {};
    uint8_t           descSetLayoutCount = 0;
    key.descSetLayouts            = descriptorSetLayouts;
    for (auto layoutHandle : descriptorSetLayouts) {
        if (layoutHandle == VK_NULL_HANDLE) {
            break;
        }
        descSetLayoutCount++;
    }

    uint32_t const pushConstantRangeCount = program->GetPushConstantRangeCount();
    auto const     pushConstantRanges     = program->GetPushConstantRanges();
    if (pushConstantRangeCount > 0) {
        LOG_ASSERT(pushConstantRangeCount <= Program::SHADER_TYPE_COUNT);
        for (uint8_t i = 0; i < pushConstantRangeCount; ++i) {
            auto const& range         = pushConstantRanges[i];
            auto&       pushConstant  = key.pushConstant[i];
            if (range.stageFlags & VK_SHADER_STAGE_VERTEX_BIT) {
                pushConstant.stage = static_cast<uint8_t>(ShaderStage::VERTEX);
            }
            if (range.stageFlags & VK_SHADER_STAGE_FRAGMENT_BIT) {
                pushConstant.stage = static_cast<uint8_t>(ShaderStage::FRAGMENT);
            }
            if (range.stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
                pushConstant.stage = static_cast<uint8_t>(ShaderStage::COMPUTE);
            }
            pushConstant.size = static_cast<uint8_t>(range.size);
        }
    }

    if (auto const iter = m_pipelineLayouts.find(key); iter != m_pipelineLayouts.end()) {
        PipelineLayoutCacheEntry& entry = iter->second;
        entry.lastUsed                  = m_timestamp++;
        return entry.handle;
    }

    VkPipelineLayoutCreateInfo info{
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext                  = nullptr,
        .setLayoutCount         = static_cast<uint32_t>(descSetLayoutCount),
        .pSetLayouts            = key.descSetLayouts.data(),
        .pushConstantRangeCount = pushConstantRangeCount,
        .pPushConstantRanges    = pushConstantRanges,
    };

    VkPipelineLayout layout = VK_NULL_HANDLE;
    vkCreatePipelineLayout(m_device, &info, kVkAlloc, &layout);

    m_pipelineLayouts.insert({ key, { layout, m_timestamp++ } });
    return layout;
}

void VulkanPipelineLayoutCache::Terminate() noexcept {
    for (auto const& [key, entry] : m_pipelineLayouts) {
        vkDestroyPipelineLayout(m_device, entry.handle, kVkAlloc);
    }
    m_pipelineLayouts.clear();
}

END_NS_BACKEND
