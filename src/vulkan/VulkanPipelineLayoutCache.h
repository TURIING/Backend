#pragma once

#include "Backend/DriverDefine.h"
#include "Backend/Program.h"

#include "Utils/Hash.h"
#include "Utils/Macro.h"

#include <cstdint>
#include <cstring>
#include <unordered_map>

#include "vulkan/VkDef.h"
#include "vulkan/VulkanAsyncHandles.h"
#include "vulkan/VulkanHandle.h"

BEGIN_NS_BACKEND

// VkPipelineLayout 的只增缓存。
//
// 布局由「描述符集布局数组 + 各 stage 的 push constant 范围」共同决定；后者不关心具体
// 偏移，只需 stage 与字节数，故键里只存这两项。
class VulkanPipelineLayoutCache {
public:
    using DescriptorSetLayoutArray = VulkanDescriptorSetLayout::DescriptorSetLayoutArray;

    explicit VulkanPipelineLayoutCache(VkDevice device) : m_device(device), m_timestamp(0) {}

    VulkanPipelineLayoutCache(VulkanPipelineLayoutCache const&)            = delete;
    VulkanPipelineLayoutCache& operator=(VulkanPipelineLayoutCache const&) = delete;

    // 须在 VkDevice 销毁前调用；语义幂等
    void Terminate() noexcept;

    struct PushConstantKey {
        // push constant 按 stage 成组，每个 stage 一组
        uint8_t stage = 0;
        uint8_t size  = 0;
        // 范围还有 offset 字段，但本项目的更新范围恒从 0 起，故不入键
    };

    struct PipelineLayoutKey {
        DescriptorSetLayoutArray descSetLayouts = {};                   // 8 * 4
        PushConstantKey          pushConstant[Program::SHADER_TYPE_COUNT] = {};  // 2 * 3
        uint16_t                 padding        = 0;
    };
    static_assert(sizeof(PipelineLayoutKey) == 40);

    NODISCARD VkPipelineLayout GetLayout(DescriptorSetLayoutArray const& descriptorSetLayouts, VulkanProgramPtr const& program);

private:
    using Timestamp = uint64_t;

    struct PipelineLayoutCacheEntry {
        VkPipelineLayout handle;
        Timestamp        lastUsed;
    };

    using PipelineLayoutKeyHashFn = NS_UTILS::hash::MurmurHashFn<PipelineLayoutKey>;

    struct PipelineLayoutKeyEqual {
        bool operator()(PipelineLayoutKey const& k1, PipelineLayoutKey const& k2) const {
            return 0 == memcmp(static_cast<void const*>(&k1), static_cast<void const*>(&k2), sizeof(PipelineLayoutKey));
        }
    };

    using PipelineLayoutMap = std::unordered_map<PipelineLayoutKey, PipelineLayoutCacheEntry, PipelineLayoutKeyHashFn, PipelineLayoutKeyEqual>;

    VkDevice          m_device;
    Timestamp         m_timestamp;
    PipelineLayoutMap m_pipelineLayouts;
};

END_NS_BACKEND
