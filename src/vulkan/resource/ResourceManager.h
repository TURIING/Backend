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
    // 句柄背书的资源：构造后由句柄持有一份引用，客户端句柄存活期间对象不被回收，返回的 SharedPtr 仅为借用视图
    template <typename D, typename B, typename... ARGS>
    NODISCARD NS_UTILS::SharedPtr<D> Make(Handle<B> const& handle, ARGS&&... args) {
        D* obj = construct<D, B>(handle, std::forward<ARGS>(args)...);
        obj->AddRef();
        return NS_UTILS::SharedPtr<D>(obj);
    }

    // 内部资源：所有权唯一归返回的 SharedPtr，不额外持有句柄引用
    template <typename D, typename... ARGS>
    NODISCARD NS_UTILS::SharedPtr<D> AllocateAndConstruct(ARGS&&... args) {
        D* obj = construct<D, D>(AllocHandle<D>(), std::forward<ARGS>(args)...);
        return NS_UTILS::SharedPtr<D>(obj);
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

    // driver 销毁入口：配 Make 使用，释放句柄持有的引用；防重复销毁
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
        // 借用视图已释放：归零则说明调用方对内部资源误用了 Destroy
        LOG_ASSERT(obj->GetRefCount() >= 1);
        obj->SubRef();
    }

    void AssociateTagToHandle(HandleBase::HandleId id, NS_UTILS::ImmutableString&& tag) noexcept;

    void Gc() noexcept;
    void Print() const noexcept;
    void Terminate() noexcept;

    // 待回收队列的长度。两条队列的区分是资源分类正确性的直接观测量，
    // 供调试与运行时验证使用
    NODISCARD size_t GetPendingGcCount() const noexcept;
    NODISCARD size_t GetPendingThreadSafeGcCount() const noexcept;

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
    mutable std::mutex m_gcListMutex;
    GcList             m_gcList;
    mutable std::mutex m_threadSafeGcListMutex;
    GcList             m_threadSafeGcList;

    friend struct Resource;
};

END_NS_BACKEND
