#pragma once

#include "Backend/Handle.h"

#include "Utils/Log.h"
#include "Utils/Utils.h"
#include "Utils/mem/SharedPtr.h"
#include "Utils/string/ImmutableString.h"

#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

#include "HandleAllocator.h"
#include "vulkan/resource/Resource.h"

BEGIN_NS_BACKEND
DECLARE_CLASS_AND_SHARE_PTR(ResourceManager);

class ResourceManager : public NS_UTILS::Ref {
public:
    ResourceManager(size_t arenaSize, bool disableUseAfterFreeCheck, bool disablePoolHandleTags);

    template <typename D>
    NODISCARD Handle<D> AllocHandle() {
        return m_handleAllocator.Allocate<D>();
    }

    // 前置条件：handle 已由 AllocHandle 分配
    template <typename D, typename B, typename... ARGS>
    NODISCARD NS_UTILS::SharedPtr<D> Make(Handle<B> const& handle, ARGS&&... args) {
        D* obj = construct<D, B>(handle, std::forward<ARGS>(args)...);
        return NS_UTILS::SharedPtr<D>(obj);
    }

    template <typename D, typename... ARGS>
    NODISCARD NS_UTILS::SharedPtr<D> AllocateAndConstruct(ARGS&&... args) {
        return Make<D, D>(AllocHandle<D>(), std::forward<ARGS>(args)...);
    }

    // handle → 对象唯一转换通道，已销毁句柄在此拦截
    template <typename D, typename B>
    NODISCARD NS_UTILS::SharedPtr<D> Acquire(Handle<B> const& handle) {
        D* obj = m_handleAllocator.HandleCast<D*, B>(handle);
        if (obj == nullptr) {
            LOG_CRITICAL("Handle id={} is invalid or of wrong type", handle.GetId());
        }
        if (obj->isDestroyed()) {
            LOG_CRITICAL("Handle id={} ({}) is being used after it has been freed", obj->GetId(), TransResourceTypeToStr(obj->GetResourceType()));
        }
        return NS_UTILS::SharedPtr<D>(obj);
    }

    // driver 销毁入口：防重复销毁，置标记后释放引用
    template <typename D>
    void Destroy(NS_UTILS::SharedPtr<D>& ptr) {
        // 用 operator bool 判空：SharedPtr 无 operator==，比较 nullptr 会经 Ref/Resource 转换产生二义
        if (!ptr) {
            LOG_CRITICAL("Destroy called with null handle");
        }
        D* obj = ptr.Get();
        if (obj->isDestroyed()) {
            LOG_CRITICAL("Resource {} id={} is destroyed twice", TransResourceTypeToStr(obj->GetResourceType()), obj->GetId());
        }
        obj->setDestroyed();
        ptr.Reset();
    }

    void AssociateTagToHandle(HandleBase::HandleId id, NS_UTILS::ImmutableString&& tag) noexcept;

    void Gc() noexcept;
    void Print() const noexcept;
    void Terminate() noexcept;

private:
    using GcList = std::vector<std::pair<ResourceType, HandleBase::HandleId>>;

    template <typename D, typename B, typename... ARGS>
    D* construct(Handle<B> const& handle, ARGS&&... args) {
        D* obj = m_handleAllocator.Construct<D, B>(handle, std::forward<ARGS>(args)...);
        obj->template init<D>(handle.GetId(), this);
        traceConstruction(obj->template GetTypeEnum<D>(), handle.GetId());
        return obj;
    }

    // 延迟销毁路径：析构对象（触发资源归还回调）并归还 HandleAllocator 池块
    template <typename D, typename B>
    void destruct(Handle<B> handle) {
        D* obj = m_handleAllocator.HandleCast<D*>(handle);
        m_handleAllocator.Deallocate(handle, obj);
    }

    void destructLaterWithType(ResourceType type, HandleBase::HandleId id);
    void destroyWithType(ResourceType type, HandleBase::HandleId id);
    void traceConstruction(ResourceType type, HandleBase::HandleId id);

    HandleAllocatorVK m_handleAllocator;
    std::mutex        m_gcListMutex;
    GcList            m_gcList;

    friend struct Resource;
};

END_NS_BACKEND
