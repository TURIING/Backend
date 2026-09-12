#pragma once

#include "Backend/DriverDefine.h"

#include "../HwDefine.h"
#include "resource/Resource.h"

#include "Utils/Macro.h"
#include "Utils/Soa.h"

#include <bitset>
#include <cstddef>
#include <cstdint>

BEGIN_NS_BACKEND

struct VulkanVertexBufferInfo : public HwVertexBufferInfo, public Resource {
    // 每位对应一个顶点属性下标，宽度须覆盖全部属性
    using AttributeBitSet = std::bitset<MAX_VERTEX_ATTRIBUTE_COUNT>;

    VulkanVertexBufferInfo(uint8_t bufferCount, uint8_t attributeCount,
            AttributeArray const& attributes);

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

        NS_UTILS::Soa<VkVertexInputAttributeDescription, VkVertexInputBindingDescription,
                VkDeviceSize, int8_t>
                m_soa;
    };

    AttributeBitSet m_attributes;
    PipelineInfo    m_info;
};

END_NS_BACKEND
