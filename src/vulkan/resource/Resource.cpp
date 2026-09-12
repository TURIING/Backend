#include "vulkan/resource/Resource.h"

#include "vulkan/resource/ResourceManager.h"

#include "Utils/Macro.h"

BEGIN_NS_BACKEND

std::string_view TransResourceTypeToStr(ResourceType type) {
    switch (type) {
        CASE_FROM_TO(ResourceType::BufferObject, "BufferObject");
        CASE_FROM_TO(ResourceType::IndexBuffer, "IndexBuffer");
        CASE_FROM_TO(ResourceType::Program, "Program");
        CASE_FROM_TO(ResourceType::RenderTarget, "RenderTarget");
        CASE_FROM_TO(ResourceType::SwapChain, "SwapChain");
        CASE_FROM_TO(ResourceType::RenderPrimitive, "RenderPrimitive");
        CASE_FROM_TO(ResourceType::Texture, "Texture");
        CASE_FROM_TO(ResourceType::TextureState, "TextureState");
        CASE_FROM_TO(ResourceType::TimerQuery, "TimerQuery");
        CASE_FROM_TO(ResourceType::VertexBuffer, "VertexBuffer");
        CASE_FROM_TO(ResourceType::VertexBufferInfo, "VertexBufferInfo");
        CASE_FROM_TO(ResourceType::DescriptorSetLayout, "DescriptorSetLayout");
        CASE_FROM_TO(ResourceType::DescriptorSet, "DescriptorSet");
        CASE_FROM_TO(ResourceType::Fence, "Fence");
        CASE_FROM_TO(ResourceType::VulkanBuffer, "VulkanBuffer");
        CASE_FROM_TO(ResourceType::StageSegment, "StageSegment");
        CASE_FROM_TO(ResourceType::StageImage, "StageImage");
        CASE_FROM_TO(ResourceType::Sync, "Sync");
        CASE_FROM_TO(ResourceType::MemoryMappedBuffer, "MemoryMappedBuffer");
        CASE_FROM_TO(ResourceType::Semaphore, "Semaphore");
        CASE_FROM_TO(ResourceType::Stream, "Stream");
        CASE_FROM_TO(ResourceType::Framebuffer, "Framebuffer");
        CASE_FROM_TO(ResourceType::RenderPass, "RenderPass");
        CASE_FROM_TO(ResourceType::UndefinedType, "");
    }
    return "";
}

void Resource::OnLastRef() {
    // 对象必须经 ResourceManager 创建（m_resManager 非空）；引用归零不立即销毁，
    // 转交其延迟回收，避免在任意线程析构
    m_resManager->destructLaterWithType(m_type, m_id);
}

template <>
ResourceType Resource::GetTypeEnum<VulkanBuffer>() noexcept {
    return ResourceType::VulkanBuffer;
}

template <>
ResourceType Resource::GetTypeEnum<VulkanVertexBufferInfo>() noexcept {
    return ResourceType::VertexBufferInfo;
}
END_NS_BACKEND
