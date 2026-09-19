#include "vulkan/VulkanDescriptorSetCache.h"

#include "vulkan/VulkanConstants.h"
#include "vulkan/commands/VulkanCommandBuffer.h"
#include "vulkan/utils/Image.h"

#include "Utils/Log.h"
#include "Utils/Panic.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

BEGIN_NS_BACKEND

namespace {

using Bitmask = VK_UTILS::UniformBufferBitmask;
static_assert(sizeof(Bitmask) * 8 == VK_UTILS::kMaxDescriptorSetBitmaskBits);

// 把位掩码的高半区（片元阶段）折到低半区，使「绑定下标」降为唯一索引
Bitmask FoldBitsInHalf(Bitmask bitset) {
    Bitmask outBitset;
    bitset.ForEachSetBit([&](size_t index) {
        constexpr size_t kBitmaskLowerBitsLen = sizeof(outBitset) * 4;
        outBitset.Set(index % kBitmaskLowerBitsLen);
    });
    return outBitset;
}

using DescriptorSetLayoutArray = VulkanDescriptorSetCache::DescriptorSetLayoutArray;
using DescriptorCount          = VulkanDescriptorSetCache::DescriptorCount;

// 按「各类描述符的个数」建池，同一布局维度的描述符集共用一池。
//
// 例如
//   layout(binding = 0, set = 1) uniform {};
//   layout(binding = 1, set = 1) sampler1;
// 与
//   layout(binding = 1, set = 2) uniform {};
//   layout(binding = 2, set = 2) sampler2;
// 维度相同，可共用同一个池
class DescriptorPool {
public:
    DescriptorPool(VkDevice device, DescriptorCount const& count, uint16_t capacity)
        : m_device(device), m_count(count), m_capacity(capacity), m_size(0), m_unusedCount(0) {
        DescriptorCount const actual = m_count * capacity;
        VkDescriptorPoolSize  sizes[4];
        uint8_t               npools = 0;
        if (actual.ubo) {
            sizes[npools++] = {
                .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = actual.ubo,
            };
        }
        if (actual.dynamicUbo) {
            sizes[npools++] = {
                .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                .descriptorCount = actual.dynamicUbo,
            };
        }
        if (actual.sampler) {
            sizes[npools++] = {
                .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = actual.sampler,
            };
        }
        if (actual.inputAttachment) {
            sizes[npools++] = {
                .type            = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
                .descriptorCount = actual.inputAttachment,
            };
        }
        VkDescriptorPoolCreateInfo info{
            .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext         = nullptr,
            .flags         = 0,
            .maxSets       = capacity,
            .poolSizeCount = npools,
            .pPoolSizes    = sizes,
        };
        vkCreateDescriptorPool(m_device, &info, kVkAlloc, &m_pool);
    }

    DescriptorPool(DescriptorPool const&)            = delete;
    DescriptorPool& operator=(DescriptorPool const&) = delete;

    ~DescriptorPool() { vkDestroyDescriptorPool(m_device, m_pool, kVkAlloc); }

    NODISCARD uint16_t const& Capacity() const { return m_capacity; }

    // 该池能否服务于给定布局维度
    NODISCARD bool CanAllocate(DescriptorCount const& count) const { return count == m_count; }

    NODISCARD VkDescriptorSet ObtainSet(VkDescriptorSetLayout vkLayout) {
        auto iter = FindSets(vkLayout);
        if (iter != m_unused.end()) {
            // 没有空闲集合时返回空句柄，由调用方决定扩容
            if (iter->second.empty()) {
                return VK_NULL_HANDLE;
            }
            std::vector<VkDescriptorSet>& sets = iter->second;
            auto const                    set  = sets.back();
            sets.pop_back();
            m_unusedCount--;
            return set;
        }
        if (m_size + 1 > m_capacity) {
            return VK_NULL_HANDLE;
        }
        VkDescriptorSetLayout      layouts[1] = { vkLayout };
        VkDescriptorSetAllocateInfo allocInfo = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext              = nullptr,
            .descriptorPool     = m_pool,
            .descriptorSetCount = 1,
            .pSetLayouts        = layouts,
        };
        VkDescriptorSet vkSet  = VK_NULL_HANDLE;
        VkResult const  result = vkAllocateDescriptorSets(m_device, &allocInfo, &vkSet);
        FILAMENT_CHECK_POSTCONDITION(result == VK_SUCCESS)
                << "Failed to allocate descriptor set code=" << result << " size=" << m_size << " capacity=" << m_capacity
                << " count=" << m_unusedCount;
        m_size++;
        return vkSet;
    }

    void Recycle(VkDescriptorSetLayout vkLayout, VkDescriptorSet vkSet) {
        // 归还集合：VkDescriptorSet 句柄不变，但后端句柄要换新，以便正确跟踪其中引用的资源
        auto iter = FindSets(vkLayout);
        if (iter != m_unused.end()) {
            iter->second.push_back(vkSet);
        } else {
            m_unused.push_back(std::make_pair(vkLayout, std::vector<VkDescriptorSet>{ vkSet }));
        }
        m_unusedCount++;
    }

private:
    using UnusedSets   = std::pair<VkDescriptorSetLayout, std::vector<VkDescriptorSet>>;
    using UnusedSetMap = std::vector<UnusedSets>;

    NODISCARD UnusedSetMap::iterator FindSets(VkDescriptorSetLayout vkLayout) {
        return std::find_if(m_unused.begin(), m_unused.end(), [vkLayout](auto const& value) { return value.first == vkLayout; });
    }

    VkDevice          m_device;
    VkDescriptorPool  m_pool      = VK_NULL_HANDLE;
    DescriptorCount const m_count;
    uint16_t const    m_capacity;

    uint16_t m_size;         // 已分配的集合总数
    uint16_t m_unusedCount;  // 空闲集合数

    UnusedSetMap m_unused;   // 布局 → 该布局下已分配但空闲的集合列表
};

}  // namespace

// 会不断扩张的描述符池集合：
//   1. 按布局维度维护若干小池；
//   2. 现有池都不兼容或耗尽时新增一个池。
class VulkanDescriptorSetCache::DescriptorInfinitePool {
private:
    static constexpr uint16_t kExpectedSetCount       = 10;
    static constexpr float    kSetCountGrowthFactor   = 1.5f;

public:
    explicit DescriptorInfinitePool(VkDevice device) : m_device(device) {}

    NODISCARD VkDescriptorSet ObtainSet(DescriptorCount const& count, VkDescriptorSetLayout vkLayout) {
        DescriptorPool* sameTypePool = nullptr;
        for (auto& pool : m_pools) {
            if (!pool->CanAllocate(count)) {
                continue;
            }
            if (auto const set = pool->ObtainSet(vkLayout); set != VK_NULL_HANDLE) {
                return set;
            }
            if (!sameTypePool || sameTypePool->Capacity() < pool->Capacity()) {
                sameTypePool = pool.get();
            }
        }

        uint16_t capacity = kExpectedSetCount;
        if (sameTypePool) {
            // 指数扩容，避免频繁走到这里
            capacity = static_cast<uint16_t>(std::ceil(static_cast<float>(sameTypePool->Capacity()) * kSetCountGrowthFactor));
        }

        m_pools.push_back(std::make_unique<DescriptorPool>(m_device, count, capacity));
        auto& pool = m_pools.back();
        auto  ret  = pool->ObtainSet(vkLayout);
        LOG_ASSERT(ret != VK_NULL_HANDLE);
        return ret;
    }

    void Recycle(DescriptorCount const& count, VkDescriptorSetLayout vkLayout, VkDescriptorSet vkSet) {
        for (auto& pool : m_pools) {
            if (!pool->CanAllocate(count)) {
                continue;
            }
            pool->Recycle(vkLayout, vkSet);
            break;
        }
    }

private:
    VkDevice                                     m_device;
    std::vector<std::unique_ptr<DescriptorPool>> m_pools;
};

VulkanDescriptorSetCache::VulkanDescriptorSetCache(VkDevice device, const ResourceManagerPtr& resourceManager)
    : m_device(device), m_resourceManager(resourceManager), m_descriptorPool(std::make_unique<DescriptorInfinitePool>(device)) {}

VulkanDescriptorSetCache::~VulkanDescriptorSetCache() = default;

void VulkanDescriptorSetCache::Terminate() noexcept {
    m_descriptorPool.reset();
    m_stashedSets   = {};
    m_lastBoundInfo = {};
}

void VulkanDescriptorSetCache::Bind(uint8_t setIndex, VulkanDescriptorSetPtr const& set, DescriptorSetOffsetArray&& offsets) {
    set->SetOffsets(std::move(offsets));
    m_stashedSets[setIndex] = set;
}

void VulkanDescriptorSetCache::Unbind(uint8_t setIndex) { m_stashedSets[setIndex] = {}; }

void VulkanDescriptorSetCache::Commit(VulkanCommandBuffer* commands, VkPipelineLayout pipelineLayout,
                                      VK_UTILS::DescriptorSetMask const& setMask) {
    // setMask 是驱动「希望」绑定的集合，curMask 是实际「需要」绑定的集合：
    // 未暂存的位与和上次绑定完全相同的位都要剔除，避免冗余的 vkCmdBindDescriptorSets
    VK_UTILS::DescriptorSetMask curMask = setMask;

    auto const& updateSets = m_stashedSets;
    curMask.ForEachSetBit([&](size_t index) {
        if (!updateSets[index]) {
            curMask.Unset(index);
        }
    });

    if (m_lastBoundInfo.pipelineLayout == pipelineLayout) {
        auto& lastBoundSets = m_lastBoundInfo.boundSets;
        curMask.ForEachSetBit([&](size_t index) {
            auto& set = updateSets[index];
            if (set.Get() == lastBoundSets[index].Get() && set->uniqueDynamicUboCount == 0) {
                curMask.Unset(index);
            }
        });
    }

    curMask.ForEachSetBit([&](size_t index) {
        auto const      set        = updateSets[index];
        VkCommandBuffer cmdBuffer  = commands->Buffer();
        VkDescriptorSet const vkSet = set->GetVkSet();
        commands->Acquire(set);
        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, static_cast<uint32_t>(index), 1, &vkSet,
                                set->uniqueDynamicUboCount, set->GetOffsets()->Data());
        set->ReferencedBy(*commands);
    });
    m_lastBoundInfo = {
        pipelineLayout,
        setMask,
        updateSets,
    };
    m_stashedSets = {};
}

void VulkanDescriptorSetCache::UpdateBuffer(VulkanDescriptorSetPtr const& set, uint8_t binding, VulkanBufferObjectPtr const& bufferObject,
                                            VkDeviceSize offset, VkDeviceSize size) noexcept {
    VkDescriptorBufferInfo const info = {
        .buffer = bufferObject->GetVkBuffer(),
        .offset = offset,
        .range  = size,
    };
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

    if (set->dynamicUboMask.Test(binding)) {
        type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    }
    VkWriteDescriptorSet descriptorWrite = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = set->GetVkSet(),
        .dstBinding      = binding,
        .descriptorCount = 1,
        .descriptorType  = type,
        .pBufferInfo     = &info,
    };
    vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
    set->Acquire(bufferObject);
}

void VulkanDescriptorSetCache::UpdateSampler(VulkanDescriptorSetPtr const& set, uint8_t binding, VulkanTexturePtr const& texture,
                                             VkSampler sampler, VkDescriptorSetLayout /*externalSamplerLayout*/) noexcept {
    VkDescriptorSet const   vkSet        = set->GetVkSet();
    VkImageSubresourceRange range        = texture->GetPrimaryViewRange();
    VkImageViewType const   expectedType = texture->TransSamplerTypeToVkImageViewType();
    if (HasAnyFlag(texture->usage, TextureUsage::DEPTH_ATTACHMENT) && expectedType == VK_IMAGE_VIEW_TYPE_2D) {
        // 带 mip 的深度纹理中某些层级可能被用作附件：此时视图范围退化为单层单级
        range.levelCount = 1;
        range.layerCount = 1;
    }
    VkDescriptorImageInfo info = {
        .sampler     = sampler,
        .imageView   = texture->GetView(range),
        .imageLayout = VK_UTILS::TransVulkanLayoutToVkImageLayout(texture->GetSamplerLayout()),
    };
    VkWriteDescriptorSet descriptorWrite = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext           = nullptr,
        .dstSet          = vkSet,
        .dstBinding      = binding,
        .descriptorCount = 1,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo      = &info,
    };
    vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
    set->Acquire(texture);
}

void VulkanDescriptorSetCache::UpdateInputAttachment(VulkanDescriptorSetPtr const& set, VulkanAttachment const& attachment) noexcept {
    // 输入附件路径尚未实现；上游在此同样为空实现
}

VulkanDescriptorSetPtr VulkanDescriptorSetCache::CreateSet(Handle<HwDescriptorSet> handle, VulkanDescriptorSetLayoutPtr const& layout) {
    auto const  vkSet    = m_descriptorPool->ObtainSet(layout->count, layout->TransVulkanLayoutToVkImageLayout());
    auto const& count    = layout->count;
    auto const  vkLayout = layout->TransVulkanLayoutToVkImageLayout();
    return m_resourceManager->Make<VulkanDescriptorSet>(
        handle, layout, [vkSet, count, vkLayout, this](VulkanDescriptorSet*) { this->ManualRecycle(count, vkLayout, vkSet); }, vkSet);
}

void VulkanDescriptorSetCache::CloneSet(VulkanDescriptorSetPtr const& set, VK_UTILS::SamplerBitmask samplerMask) noexcept {
    auto const& layout = set->GetLayout();
    VkDescriptorSetLayout const genLayout = set->boundLayout;  // 按当前绑定布局新建集合
    VkDescriptorSet const       newSet    = GetVkSet(layout->count, genLayout);

    // 每个位掩码的位是「绑定下标」，且顶点/片元两阶段分列高低半区；
    // 折到低半区后即得到一份合并后的绑定集合
    Bitmask const ubo = layout->bitmask.ubo | layout->bitmask.dynamicUbo;
    Bitmask const samplers     = layout->bitmask.sampler ^ samplerMask;  // samplerMask 中的采样器不拷贝
    Bitmask const copyBindings = FoldBitsInHalf(ubo | samplers);

    VkDescriptorSet const srcSet = set->GetVkSet();
    CopySet(srcSet, newSet, copyBindings);
    set->AddNewSet(newSet, [this, layoutCount = layout->count, genLayout, newSet](VulkanDescriptorSet*) {
        this->ManualRecycle(layoutCount, genLayout, newSet);
    });
}

VkDescriptorSet VulkanDescriptorSetCache::GetVkSet(DescriptorCount const& count, VkDescriptorSetLayout vkLayout) {
    return m_descriptorPool->ObtainSet(count, vkLayout);
}

void VulkanDescriptorSetCache::ManualRecycle(VulkanDescriptorSetLayout::Count const& count, VkDescriptorSetLayout vkLayout,
                                             VkDescriptorSet vkSet) {
    // 后端关闭时 m_descriptorPool 可能已经销毁
    if (m_descriptorPool) {
        m_descriptorPool->Recycle(count, vkLayout, vkSet);
    }
}

void VulkanDescriptorSetCache::Gc() { m_stashedSets = {}; }

void VulkanDescriptorSetCache::CopySet(VkDescriptorSet srcSet, VkDescriptorSet dstSet, VK_UTILS::SamplerBitmask bindings) const {
    std::vector<VkCopyDescriptorSet> copies;
    bindings.ForEachSetBit([&](size_t index) {
        copies.push_back({
            .sType           = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET,
            .srcSet          = srcSet,
            .srcBinding      = static_cast<uint32_t>(index),
            .dstSet          = dstSet,
            .dstBinding      = static_cast<uint32_t>(index),
            .descriptorCount = 1,
        });
    });
    vkUpdateDescriptorSets(m_device, 0, nullptr, static_cast<uint32_t>(copies.size()), copies.data());
}

END_NS_BACKEND
