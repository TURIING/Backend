#pragma once

#include "Backend/DriverDefine.h"

#include "Utils/Macro.h"
#include "Utils/Soa.h"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../HwDefine.h"
#include "buffer/VulkanBufferProxy.h"
#include "resource/Resource.h"
#include "stage/VulkanStagePool.h"
#include "vulkan/VulkanContext.h"

BEGIN_NS_BACKEND

struct VulkanCommandBuffer;

struct VulkanVertexBufferInfo : public HwVertexBufferInfo, public Resource {
    // 每位对应一个顶点属性下标，宽度须覆盖全部属性
    using AttributeBitSet = std::bitset<MAX_VERTEX_ATTRIBUTE_COUNT>;

    VulkanVertexBufferInfo(uint8_t bufferCount, uint8_t attributeCount, AttributeArray const& attributes);

    NODISCARD VkVertexInputAttributeDescription const* GetAttribDescriptions() const noexcept {
        auto const& soa = m_info.m_soa;
        return soa.Data<PipelineInfo::kAttributeDescription>();
    }

    NODISCARD VkVertexInputBindingDescription const* GetBufferDescriptions() const noexcept {
        auto const& soa = m_info.m_soa;
        return soa.Data<PipelineInfo::kBufferDescription>();
    }

    NODISCARD int8_t const* GetAttributeToBuffer() const noexcept {
        auto const& soa = m_info.m_soa;
        return soa.Data<PipelineInfo::kAttributeToBufferIndex>();
    }

    NODISCARD VkDeviceSize const* GetOffsets() const noexcept {
        auto const& soa = m_info.m_soa;
        return soa.Data<PipelineInfo::kOffsets>();
    }

    NODISCARD size_t GetAttributeCount() const noexcept { return m_info.m_soa.Size(); }

    NODISCARD AttributeBitSet GetDeclaredAttributes() const noexcept { return m_attributes; }

private:
    struct PipelineInfo {
        PipelineInfo(size_t size) : m_soa(size) { m_soa.Resize(size); }

        // 列下标即 SoA 模板实参序号
        static constexpr size_t kAttributeDescription   = 0;
        static constexpr size_t kBufferDescription      = 1;
        static constexpr size_t kOffsets                = 2;
        static constexpr size_t kAttributeToBufferIndex = 3;

        NS_UTILS::Soa<VkVertexInputAttributeDescription, VkVertexInputBindingDescription, VkDeviceSize, int8_t> m_soa;
    };

    AttributeBitSet m_attributes;
    PipelineInfo    m_info;
};
DECLARE_SHARE_PTR_CLASS(VulkanVertexBufferInfo);

struct VulkanBufferObject : public HwBufferObject, public Resource {
    VulkanBufferObject(const VulkanContextPtr& context, VmaAllocator allocator, const VulkanStagePoolPtr& stagePool,
                       const VulkanBufferCachePtr& bufferCache, uint32_t byteCount, BufferObjectBinding bindingType, BufferUsage usage);

    // 上传通道尚未移植：内容不会写入 GPU，调用方读到的是缓冲初始内容
    void LoadFromCpu(VulkanCommandBuffer& commands, void const* cpuData, uint32_t byteOffset, uint32_t numBytes);

    NODISCARD VkBuffer GetVkBuffer() const noexcept { return m_buffer.GetVkBuffer(); }

    BufferObjectBinding const bindingType;

private:
    VulkanBufferProxy m_buffer;
};
DECLARE_SHARE_PTR_CLASS(VulkanBufferObject);

struct VulkanIndexBuffer : public HwIndexBuffer, public Resource {
    VulkanIndexBuffer(const VulkanContextPtr& context, VmaAllocator allocator, const VulkanStagePoolPtr& stagePool,
                      const VulkanBufferCachePtr& bufferCache, uint8_t elementSize, uint32_t indexCount);

    NODISCARD VkBuffer GetVkBuffer() const noexcept { return m_buffer.GetVkBuffer(); }

    VkIndexType const indexType;

private:
    VulkanBufferProxy m_buffer;
};
DECLARE_SHARE_PTR_CLASS(VulkanIndexBuffer);

struct VulkanVertexBuffer : public HwVertexBuffer, public Resource {
    VulkanVertexBuffer(const VulkanContextPtr& context, const VulkanBufferCachePtr& bufferCache, uint32_t vertexCount,
                       VulkanVertexBufferInfoPtr vbi);

    void SetBuffer(const VulkanBufferObjectPtr& bufferObject, uint32_t index);

    // 缓冲代理可在运行期换掉底层 VkBuffer，故不缓存句柄作优化，只能取当前值
    NODISCARD VkBuffer const* GetVkBuffers() const noexcept { return m_buffers.data(); }
    NODISCARD VkBuffer* GetVkBuffers() noexcept { return m_buffers.data(); }

    // 声明的属性是否全部已挂上缓冲
    NODISCARD bool IsValid() const noexcept { return m_attributes == vbi->GetDeclaredAttributes(); }

    VulkanVertexBufferInfoPtr vbi;

private:
    std::vector<VkBuffer>           m_buffers;
    std::vector<VulkanBufferObjectPtr> m_resources;
    VulkanVertexBufferInfo::AttributeBitSet m_attributes;
};

END_NS_BACKEND
