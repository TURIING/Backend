#pragma once

#include "Backend/DriverDefine.h"
#include "Backend/Program.h"
#include "Backend/TargetBufferInfo.h"

#include "Utils/Macro.h"
#include "Utils/Utils.h"

#include <array>
#include <cstdint>
#include <memory>
#include <utility>

#include "vulkan/VkDef.h"
#include "vulkan/VulkanHandle.h"
#include "vulkan/resource/ResourceManager.h"
#include "vulkan/utils/Definitions.h"

BEGIN_NS_BACKEND

// 描述符池之上的抽象层：负责描述符集的分配、复用、绑定状态与提交。
class VulkanDescriptorSetCache {
public:
    static constexpr uint8_t kUniqueDescriptorSetCount = VulkanDescriptorSetLayout::kUniqueDescriptorSetCount;

    using DescriptorSetLayoutArray = VulkanDescriptorSetLayout::DescriptorSetLayoutArray;
    using DescriptorSetArray       = std::array<VulkanDescriptorSetPtr, kUniqueDescriptorSetCount>;
    using DescriptorCount          = VulkanDescriptorSetLayout::Count;

    VulkanDescriptorSetCache(VkDevice device, const ResourceManagerPtr& resourceManager);
    ~VulkanDescriptorSetCache();

    VulkanDescriptorSetCache(VulkanDescriptorSetCache const&)            = delete;
    VulkanDescriptorSetCache& operator=(VulkanDescriptorSetCache const&) = delete;

    // 须在 VkDevice 销毁前调用
    void Terminate() noexcept;

    void UpdateBuffer(VulkanDescriptorSetPtr const& set, uint8_t binding, VulkanBufferObjectPtr const& bufferObject, VkDeviceSize offset,
                      VkDeviceSize size) noexcept;

    void UpdateSampler(VulkanDescriptorSetPtr const& set, uint8_t binding, VulkanTexturePtr const& texture, VkSampler sampler,
                       VkDescriptorSetLayout externalSamplerLayout = VK_NULL_HANDLE) noexcept;

    void UpdateInputAttachment(VulkanDescriptorSetPtr const& set, VulkanAttachment const& attachment) noexcept;

    // 此处并不真正绑定，只是暂存；待拿到 VkPipelineLayout 后由 Commit 一并提交
    void Bind(uint8_t setIndex, VulkanDescriptorSetPtr const& set, DescriptorSetOffsetArray&& offsets);

    void Unbind(uint8_t setIndex);

    void Commit(VulkanCommandBuffer* commands, VkPipelineLayout pipelineLayout, VK_UTILS::DescriptorSetMask const& setMask);

    NODISCARD VulkanDescriptorSetPtr CreateSet(Handle<HwDescriptorSet> handle, VulkanDescriptorSetLayoutPtr const& layout);

    // 基于当前绑定布局新建一个 VkDescriptorSet，并把 samplerMask 之外的绑定拷贝过去
    void CloneSet(VulkanDescriptorSetPtr const& set, VK_UTILS::SamplerBitmask samplerMask) noexcept;

    // 外部采样器路径使用：按描述符计数取一个集合
    NODISCARD VkDescriptorSet GetVkSet(DescriptorCount const& count, VkDescriptorSetLayout vkLayout);

    void ManualRecycle(VulkanDescriptorSetLayout::Count const& count, VkDescriptorSetLayout vkLayout, VkDescriptorSet vkSet);

    NODISCARD DescriptorSetArray const& GetBoundSets() const { return m_stashedSets; }

    void Gc();

    void ResetCachedState() noexcept { m_lastBoundInfo = {}; }

private:
    void CopySet(VkDescriptorSet srcSet, VkDescriptorSet destSet, VK_UTILS::SamplerBitmask copyBindings) const;

    class DescriptorInfinitePool;

    VkDevice           m_device;
    ResourceManagerPtr m_resourceManager;
    std::unique_ptr<DescriptorInfinitePool> m_descriptorPool;
    DescriptorSetArray m_stashedSets = {};

    struct {
        VkPipelineLayout             pipelineLayout = VK_NULL_HANDLE;
        VK_UTILS::DescriptorSetMask  setMask;
        DescriptorSetArray           boundSets = {};
    } m_lastBoundInfo;
};

END_NS_BACKEND
