#include "VulkanHandle.h"

#include "Utils/Log.h"
#include "Utils/Macro.h"

#include "../Macro.h"

BEGIN_NS_BACKEND

namespace {

// 应用层用途到 buffer 域池类型的映射：两类缓冲对象共用池，各自用途决定 VkBuffer usage
VulkanBufferBinding GetBufferObjectBinding(BufferObjectBinding bindingType) noexcept {
    switch (bindingType) {
        CASE_FROM_TO(BufferObjectBinding::Vertex, VulkanBufferBinding::Vertex);
        CASE_FROM_TO(BufferObjectBinding::Uniform, VulkanBufferBinding::Uniform);
        CASE_FROM_TO(BufferObjectBinding::ShaderStorage, VulkanBufferBinding::ShaderStorage);
    }
    return VulkanBufferBinding::Unknown;
}

VkFormat GetVkFormat(ElementType type, bool normalized, bool integer) noexcept {
    if (normalized) {
        switch (type) {
            CASE_FROM_TO(ElementType::BYTE, VK_FORMAT_R8_SNORM);
            CASE_FROM_TO(ElementType::UBYTE, VK_FORMAT_R8_UNORM);
            CASE_FROM_TO(ElementType::SHORT, VK_FORMAT_R16_SNORM);
            CASE_FROM_TO(ElementType::USHORT, VK_FORMAT_R16_UNORM);
            CASE_FROM_TO(ElementType::BYTE2, VK_FORMAT_R8G8_SNORM);
            CASE_FROM_TO(ElementType::UBYTE2, VK_FORMAT_R8G8_UNORM);
            CASE_FROM_TO(ElementType::SHORT2, VK_FORMAT_R16G16_SNORM);
            CASE_FROM_TO(ElementType::USHORT2, VK_FORMAT_R16G16_UNORM);
            CASE_FROM_TO(ElementType::BYTE3, VK_FORMAT_R8G8B8_SNORM);
            CASE_FROM_TO(ElementType::UBYTE3, VK_FORMAT_R8G8B8_UNORM);
            CASE_FROM_TO(ElementType::SHORT3, VK_FORMAT_R16G16B16_SNORM);
            CASE_FROM_TO(ElementType::USHORT3, VK_FORMAT_R16G16B16_UNORM);
            CASE_FROM_TO(ElementType::BYTE4, VK_FORMAT_R8G8B8A8_SNORM);
            CASE_FROM_TO(ElementType::UBYTE4, VK_FORMAT_R8G8B8A8_UNORM);
            CASE_FROM_TO(ElementType::SHORT4, VK_FORMAT_R16G16B16A16_SNORM);
            CASE_FROM_TO(ElementType::USHORT4, VK_FORMAT_R16G16B16A16_UNORM);
            default:
                LOG_CRITICAL("No normalized format for ElementType {}", static_cast<int>(type));
                return VK_FORMAT_UNDEFINED;
        }
    }

    switch (type) {
        CASE_FROM_TO(ElementType::BYTE, integer ? VK_FORMAT_R8_SINT : VK_FORMAT_R8_SSCALED);
        CASE_FROM_TO(ElementType::UBYTE, integer ? VK_FORMAT_R8_UINT : VK_FORMAT_R8_USCALED);
        CASE_FROM_TO(ElementType::SHORT, integer ? VK_FORMAT_R16_SINT : VK_FORMAT_R16_SSCALED);
        CASE_FROM_TO(ElementType::USHORT, integer ? VK_FORMAT_R16_UINT : VK_FORMAT_R16_USCALED);
        CASE_FROM_TO(ElementType::HALF, VK_FORMAT_R16_SFLOAT);
        CASE_FROM_TO(ElementType::INT, VK_FORMAT_R32_SINT);
        CASE_FROM_TO(ElementType::UINT, VK_FORMAT_R32_UINT);
        CASE_FROM_TO(ElementType::FLOAT, VK_FORMAT_R32_SFLOAT);
        CASE_FROM_TO(ElementType::BYTE2, integer ? VK_FORMAT_R8G8_SINT : VK_FORMAT_R8G8_SSCALED);
        CASE_FROM_TO(ElementType::UBYTE2, integer ? VK_FORMAT_R8G8_UINT : VK_FORMAT_R8G8_USCALED);
        CASE_FROM_TO(ElementType::SHORT2, integer ? VK_FORMAT_R16G16_SINT : VK_FORMAT_R16G16_SSCALED);
        CASE_FROM_TO(ElementType::USHORT2, integer ? VK_FORMAT_R16G16_UINT : VK_FORMAT_R16G16_USCALED);
        CASE_FROM_TO(ElementType::HALF2, VK_FORMAT_R16G16_SFLOAT);
        CASE_FROM_TO(ElementType::FLOAT2, VK_FORMAT_R32G32_SFLOAT);
        CASE_FROM_TO(ElementType::BYTE3, integer ? VK_FORMAT_R8G8B8_SINT : VK_FORMAT_R8G8B8_SSCALED);
        CASE_FROM_TO(ElementType::UBYTE3, integer ? VK_FORMAT_R8G8B8_UINT : VK_FORMAT_R8G8B8_USCALED);
        CASE_FROM_TO(ElementType::SHORT3, integer ? VK_FORMAT_R16G16B16_SINT : VK_FORMAT_R16G16B16_SSCALED);
        CASE_FROM_TO(ElementType::USHORT3, integer ? VK_FORMAT_R16G16B16_UINT : VK_FORMAT_R16G16B16_USCALED);
        CASE_FROM_TO(ElementType::HALF3, VK_FORMAT_R16G16B16_SFLOAT);
        CASE_FROM_TO(ElementType::FLOAT3, VK_FORMAT_R32G32B32_SFLOAT);
        CASE_FROM_TO(ElementType::BYTE4, integer ? VK_FORMAT_R8G8B8A8_SINT : VK_FORMAT_R8G8B8A8_SSCALED);
        CASE_FROM_TO(ElementType::UBYTE4, integer ? VK_FORMAT_R8G8B8A8_UINT : VK_FORMAT_R8G8B8A8_USCALED);
        CASE_FROM_TO(ElementType::SHORT4, integer ? VK_FORMAT_R16G16B16A16_SINT : VK_FORMAT_R16G16B16A16_SSCALED);
        CASE_FROM_TO(ElementType::USHORT4, integer ? VK_FORMAT_R16G16B16A16_UINT : VK_FORMAT_R16G16B16A16_USCALED);
        CASE_FROM_TO(ElementType::HALF4, VK_FORMAT_R16G16B16A16_SFLOAT);
        CASE_FROM_TO(ElementType::FLOAT4, VK_FORMAT_R32G32B32A32_SFLOAT);
    }
    return VK_FORMAT_UNDEFINED;
}

}  // namespace

VulkanVertexBufferInfo::VulkanVertexBufferInfo(uint8_t bufferCount, uint8_t attributeCount, AttributeArray const& attributes)
    : HwVertexBufferInfo(bufferCount, attributeCount), m_info(attributeCount == 0 ? 0 : attributes.size()) {
    // 无属性绘制：顶点属性/绑定描述数量为 0 是 Vulkan 无属性 draw 的前提，无需填充
    EARLY_RETURN(attributeCount == 0);

    auto&                                    soa                 = m_info.m_soa;
    VkVertexInputAttributeDescription* const attribDesc          = soa.Data<PipelineInfo::kAttributeDescription>();
    VkVertexInputBindingDescription* const   bufferDesc          = soa.Data<PipelineInfo::kBufferDescription>();
    VkDeviceSize* const                      offsets             = soa.Data<PipelineInfo::kOffsets>();
    int8_t* const                            attribToBufferIndex = soa.Data<PipelineInfo::kAttributeToBufferIndex>();

    for (uint32_t attribIndex = 0; attribIndex < attributes.size(); attribIndex++) {
        Attribute  attrib       = attributes[attribIndex];
        bool const isInteger    = attrib.flags & Attribute::FLAG_INTEGER;
        bool const isNormalized = attrib.flags & Attribute::FLAG_NORMALIZED;
        VkFormat   vkformat     = GetVkFormat(attrib.type, isNormalized, isInteger);

        // 未绑缓冲的属性复用位置缓冲布局：顶点着色器按 vec4 声明属性，位置元素至少 32bit
        if (attrib.buffer == Attribute::BUFFER_UNUSED) {
            vkformat = isInteger ? VK_FORMAT_R8G8B8A8_UINT : VK_FORMAT_R8G8B8A8_SNORM;
            attrib   = attributes[0];
        }
        offsets[attribIndex] = attrib.offset;
        // location/binding 与 GLSL layout specifier、vkCmdBindVertexBuffers 绑定位置一一对应
        attribDesc[attribIndex] = {
            .location = attribIndex,
            .binding  = attribIndex,
            .format   = vkformat,
        };
        bufferDesc[attribIndex] = {
            .binding = attribIndex,
            .stride  = attrib.stride,
        };
        attribToBufferIndex[attribIndex] = static_cast<int8_t>(attrib.buffer);
        m_attributes.set(attribIndex);
    }
}

VulkanBufferObject::VulkanBufferObject(const VulkanContextPtr& context, VmaAllocator allocator, const VulkanStagePoolPtr& stagePool,
                                       const VulkanBufferCachePtr& bufferCache, uint32_t byteCount, BufferObjectBinding bindingType,
                                       BufferUsage usage)
    : HwBufferObject(byteCount, false),
      bindingType(bindingType),
      m_buffer(context, allocator, stagePool, bufferCache, GetBufferObjectBinding(bindingType), usage, byteCount) {}

void VulkanBufferObject::LoadFromCpu(VulkanCommandBuffer& commands, void const* cpuData, uint32_t byteOffset, uint32_t numBytes) {}

VulkanIndexBuffer::VulkanIndexBuffer(const VulkanContextPtr& context, VmaAllocator allocator, const VulkanStagePoolPtr& stagePool,
                                     const VulkanBufferCachePtr& bufferCache, uint8_t elementSize, uint32_t indexCount)
    : HwIndexBuffer(elementSize, indexCount, false),
      indexType(elementSize == sizeof(uint16_t) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32),
      m_buffer(context, allocator, stagePool, bufferCache, VulkanBufferBinding::Index, BufferUsage::STATIC,
               static_cast<uint32_t>(elementSize) * indexCount) {}

END_NS_BACKEND
