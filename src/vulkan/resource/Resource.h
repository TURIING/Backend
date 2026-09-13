#pragma once

#include "Backend/Handle.h"

#include "Utils/Utils.h"
#include "Utils/mem/Ref.h"

#include <cstdint>
#include <string_view>

BEGIN_NS_BACKEND

class ResourceManager;
class VulkanBuffer;
struct VulkanSemaphore;
struct VulkanVertexBufferInfo;
struct VulkanVertexBuffer;

enum class ResourceType : uint8_t {
    BufferObject        = 0,
    IndexBuffer         = 1,
    Program             = 2,
    RenderTarget        = 3,
    SwapChain           = 4,
    RenderPrimitive     = 5,
    Texture             = 6,
    TextureState        = 7,
    TimerQuery          = 8,
    VertexBuffer        = 9,
    VertexBufferInfo    = 10,
    DescriptorSetLayout = 11,
    DescriptorSet       = 12,
    Fence               = 13,
    VulkanBuffer        = 14,
    StageSegment        = 15,
    StageImage          = 16,
    Sync                = 17,
    MemoryMappedBuffer  = 18,
    Semaphore           = 19,
    Stream              = 20,
    Framebuffer         = 21,
    RenderPass          = 22,
    // 末位哨兵，枚举迭代依赖
    UndefinedType = 23,
};

std::string_view TransResourceTypeToStr(ResourceType type);

struct Resource : public NS_UTILS::Ref {
    Resource() : m_resManager(nullptr), m_id(HandleBase::kNullId), m_type(ResourceType::UndefinedType), m_destroyed(false) {}

    template <typename D>
    NODISCARD bool IsType() const {
        return GetTypeEnum<D>() == m_type;
    }

    NODISCARD HandleBase::HandleId GetId() const { return m_id; }
    NODISCARD ResourceType GetResourceType() const { return m_type; }


protected:
    void OnLastRef() override;

private:
    template <typename D>
    void init(HandleBase::HandleId id, ResourceManager* resManager) {
        m_id         = id;
        m_resManager = resManager;
        m_type       = GetTypeEnum<D>();
    }

    void setDestroyed() { m_destroyed = true; }
    NODISCARD bool isDestroyed() const { return m_destroyed; }

    // 具体类型的特化由 VulkanHandles 移植补齐
    template <typename D>
    ResourceType GetTypeEnum() noexcept {
        return ResourceType::UndefinedType;
    }

    ResourceManager*     m_resManager;
    HandleBase::HandleId m_id;
    ResourceType         m_type;
    bool                 m_destroyed;

    friend class ResourceManager;
};
DECLARE_SHARE_PTR_CLASS(Resource);

template <>
ResourceType Resource::GetTypeEnum<VulkanBuffer>() noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanVertexBufferInfo>() noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanVertexBuffer>() noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanSemaphore>() noexcept;

END_NS_BACKEND
