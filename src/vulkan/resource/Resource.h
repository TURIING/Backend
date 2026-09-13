#pragma once

#include "Backend/Handle.h"

#include "Utils/Utils.h"
#include "Utils/mem/Ref.h"

#include <cstdint>
#include <string_view>

BEGIN_NS_BACKEND

class ResourceManager;
class VulkanBuffer;
struct VulkanBufferObject;
struct VulkanDescriptorSet;
struct VulkanDescriptorSetLayout;
struct VulkanFence;
struct VulkanFramebuffer;
struct VulkanIndexBuffer;
struct VulkanMemoryMappedBuffer;
struct VulkanProgram;
struct VulkanRenderPass;
struct VulkanRenderPrimitive;
struct VulkanRenderTarget;
struct VulkanSemaphore;
struct VulkanSwapChain;
struct VulkanSync;
struct VulkanTexture;
struct VulkanTextureState;
struct VulkanTimerQuery;
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

// 该类型的销毁须经线程安全队列入队。
//
// 分类依据是运行期的 ResourceType 而非模板参数 —— Resource::OnLastRef() 只拿得到 m_type。
// 新增线程安全类型时，此处与类型定义处的 ThreadSafeResource 基类必须成对出现。
constexpr bool IsThreadSafeType(ResourceType type) noexcept {
    switch (type) {
        CASE_FROM_TO(ResourceType::Program, true)
        CASE_FROM_TO(ResourceType::Fence, true)
        CASE_FROM_TO(ResourceType::Sync, true)
        CASE_FROM_TO(ResourceType::TimerQuery, true)
        default:
            return false;
    }
}

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
    ResourceType GetTypeEnum() const noexcept {
        return ResourceType::UndefinedType;
    }

    ResourceManager*     m_resManager;
    HandleBase::HandleId m_id;
    ResourceType         m_type;
    bool                 m_destroyed;

    friend class ResourceManager;
};
DECLARE_SHARE_PTR_CLASS(Resource);

// 语义标记：该类型的引用归零可能发生在非 backend 线程（如编译线程），故销毁须入独立队列
//
// 不引入第二套引用计数 —— NS_UTILS::Ref 已用 std::atomic<int32_t> 计数；也不重写
// OnLastRef()，归零动作仍由 Resource 统一处理，入哪个队列由 IsThreadSafeType 决定
struct ThreadSafeResource : public Resource {};

template <>
ResourceType Resource::GetTypeEnum<VulkanBuffer>() const noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanBufferObject>() const noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanIndexBuffer>() const noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanVertexBufferInfo>() const noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanVertexBuffer>() const noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanSemaphore>() const noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanTexture>() const noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanTextureState>() const noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanSwapChain>() const noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanRenderTarget>() const noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanFramebuffer>() const noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanRenderPass>() const noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanProgram>() const noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanFence>() const noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanSync>() const noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanTimerQuery>() const noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanDescriptorSetLayout>() const noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanDescriptorSet>() const noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanRenderPrimitive>() const noexcept;

template <>
ResourceType Resource::GetTypeEnum<VulkanMemoryMappedBuffer>() const noexcept;

END_NS_BACKEND
