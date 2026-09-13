#include <cstdint>
#include <utility>

#include "VulkanHandle.h"

BEGIN_NS_BACKEND

VulkanVertexBuffer::VulkanVertexBuffer(const VulkanContextPtr& context, const VulkanBufferCachePtr& bufferCache, uint32_t vertexCount,
                                       VulkanVertexBufferInfoPtr vbi)
    : HwVertexBuffer(vertexCount),
      vbi(std::move(vbi)),
      // 预留全部绑定槽位：SetBuffer 按属性下标写入，槽位不足会越界
      m_buffers(MAX_VERTEX_BUFFER_COUNT) {}

void VulkanVertexBuffer::SetBuffer(const VulkanBufferObjectPtr& bufferObject, uint32_t index) {
    uint8_t const       count          = static_cast<uint8_t>(vbi->GetAttributeCount());
    int8_t const* const attribToBuffer = vbi->GetAttributeToBuffer();

    for (uint8_t attribIndex = 0; attribIndex < count; attribIndex++) {
        if (attribToBuffer[attribIndex] == static_cast<int8_t>(index)) {
            m_buffers[attribIndex] = bufferObject->GetVkBuffer();
            m_attributes.set(attribIndex);
        }
    }
    m_resources.push_back(bufferObject);
}

END_NS_BACKEND
