#pragma once

#include "Backend/DriverDefine.h"

#include "Utils/Log.h"

#include <cstdint>
#include <type_traits>
#include <utility>

BEGIN_NS_BACKEND

struct HwBufferObject;
struct HwFence;
struct HwIndexBuffer;
struct HwProgram;
struct HwRenderPrimitive;
struct HwRenderTarget;
struct HwStream;
struct HwSwapChain;
struct HwSync;
struct HwTexture;
struct HwTimerQuery;
struct HwVertexBufferInfo;
struct HwVertexBuffer;
struct HwDescriptorSetLayout;
struct HwDescriptorSet;
struct HwMemoryMappedBuffer;

class HandleBase {
public:
    using HandleId                    = uint32_t;
    static constexpr HandleId kNullId = HandleId{ UINT32_MAX };
    constexpr HandleBase() noexcept : m_object(kNullId) {}
    explicit               operator bool() const noexcept { return m_object != kNullId; }
    void                   Clear() noexcept { m_object = kNullId; }
    [[nodiscard]] HandleId GetId() const noexcept { return m_object; }

    // 内部使用：以 nullid 构造说明未初始化句柄被使用
    explicit HandleBase(HandleId id) noexcept : m_object(id) { LOG_ASSERT(m_object != kNullId); }

protected:
    HandleBase(HandleBase const& rhs) noexcept            = default;
    HandleBase& operator=(HandleBase const& rhs) noexcept = default;

    HandleBase(HandleBase&& rhs) noexcept : m_object(rhs.m_object) { rhs.m_object = kNullId; }

    HandleBase& operator=(HandleBase&& rhs) noexcept {
        if (this != &rhs) {
            m_object     = rhs.m_object;
            rhs.m_object = kNullId;
        }
        return *this;
    }

private:
    HandleId m_object;
};

template <typename T>
struct Handle : public HandleBase {
    Handle() noexcept = default;

    Handle(Handle const& rhs) noexcept = default;
    Handle(Handle&& rhs) noexcept      = default;

    // 显式调用基类版本，规避旧编译器移动时不调用父类方法的问题
    Handle& operator=(Handle const& rhs) noexcept {
        HandleBase::operator=(rhs);
        return *this;
    }
    Handle& operator=(Handle&& rhs) noexcept {
        HandleBase::operator=(std::move(rhs));
        return *this;
    }

    explicit Handle(HandleId id) noexcept : HandleBase(id) {}

    bool operator==(Handle const& rhs) const noexcept { return GetId() == rhs.GetId(); }
    bool operator!=(Handle const& rhs) const noexcept { return GetId() != rhs.GetId(); }
    bool operator<(Handle const& rhs) const noexcept { return GetId() < rhs.GetId(); }
    bool operator<=(Handle const& rhs) const noexcept { return GetId() <= rhs.GetId(); }
    bool operator>(Handle const& rhs) const noexcept { return GetId() > rhs.GetId(); }
    bool operator>=(Handle const& rhs) const noexcept { return GetId() >= rhs.GetId(); }

    template <typename B, typename = std::enable_if_t<std::is_base_of_v<T, B>>>
    Handle(Handle<B> const& base) noexcept : HandleBase(base) {}
};

using BufferObjectHandle        = Handle<HwBufferObject>;
using FenceHandle               = Handle<HwFence>;
using IndexBufferHandle         = Handle<HwIndexBuffer>;
using ProgramHandle             = Handle<HwProgram>;
using RenderPrimitiveHandle     = Handle<HwRenderPrimitive>;
using RenderTargetHandle        = Handle<HwRenderTarget>;
using StreamHandle              = Handle<HwStream>;
using SwapChainHandle           = Handle<HwSwapChain>;
using SyncHandle                = Handle<HwSync>;
using TextureHandle             = Handle<HwTexture>;
using TimerQueryHandle          = Handle<HwTimerQuery>;
using VertexBufferHandle        = Handle<HwVertexBuffer>;
using VertexBufferInfoHandle    = Handle<HwVertexBufferInfo>;
using DescriptorSetLayoutHandle = Handle<HwDescriptorSetLayout>;
using DescriptorSetHandle       = Handle<HwDescriptorSet>;
using MemoryMappedBufferHandle  = Handle<HwMemoryMappedBuffer>;

END_NS_BACKEND
