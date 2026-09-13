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
ResourceType Resource::GetTypeEnum<VulkanBuffer>() const noexcept {
    return ResourceType::VulkanBuffer;
}

template <>
ResourceType Resource::GetTypeEnum<VulkanBufferObject>() const noexcept {
    return ResourceType::BufferObject;
}

template <>
ResourceType Resource::GetTypeEnum<VulkanIndexBuffer>() const noexcept {
    return ResourceType::IndexBuffer;
}

template <>
ResourceType Resource::GetTypeEnum<VulkanVertexBufferInfo>() const noexcept {
    return ResourceType::VertexBufferInfo;
}

template <>
ResourceType Resource::GetTypeEnum<VulkanVertexBuffer>() const noexcept {
    return ResourceType::VertexBuffer;
}

template <>
ResourceType Resource::GetTypeEnum<VulkanSemaphore>() const noexcept {
    return ResourceType::Semaphore;
}

template <>
ResourceType Resource::GetTypeEnum<VulkanTexture>() const noexcept {
    return ResourceType::Texture;
}

template <>
ResourceType Resource::GetTypeEnum<VulkanTextureState>() const noexcept {
    return ResourceType::TextureState;
}

template <>
ResourceType Resource::GetTypeEnum<VulkanSwapChain>() const noexcept {
    return ResourceType::SwapChain;
}

template <>
ResourceType Resource::GetTypeEnum<VulkanRenderTarget>() const noexcept {
    return ResourceType::RenderTarget;
}

template <>
ResourceType Resource::GetTypeEnum<VulkanFramebuffer>() const noexcept {
    return ResourceType::Framebuffer;
}

template <>
ResourceType Resource::GetTypeEnum<VulkanRenderPass>() const noexcept {
    return ResourceType::RenderPass;
}

template <>
ResourceType Resource::GetTypeEnum<VulkanProgram>() const noexcept {
    return ResourceType::Program;
}

template <>
ResourceType Resource::GetTypeEnum<VulkanFence>() const noexcept {
    return ResourceType::Fence;
}

template <>
ResourceType Resource::GetTypeEnum<VulkanSync>() const noexcept {
    return ResourceType::Sync;
}

template <>
ResourceType Resource::GetTypeEnum<VulkanTimerQuery>() const noexcept {
    return ResourceType::TimerQuery;
}

template <>
ResourceType Resource::GetTypeEnum<VulkanDescriptorSetLayout>() const noexcept {
    return ResourceType::DescriptorSetLayout;
}

template <>
ResourceType Resource::GetTypeEnum<VulkanDescriptorSet>() const noexcept {
    return ResourceType::DescriptorSet;
}

template <>
ResourceType Resource::GetTypeEnum<VulkanMemoryMappedBuffer>() const noexcept {
    return ResourceType::MemoryMappedBuffer;
}

template <>
ResourceType Resource::GetTypeEnum<VulkanRenderPrimitive>() const noexcept {
    return ResourceType::RenderPrimitive;
}

END_NS_BACKEND
