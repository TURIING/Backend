#pragma once

#include "Backend/Handle.h"

#include "Utils/Arena/Allocator.h"
#include "Utils/Arena/Area.h"
#include "Utils/Arena/Arena.h"
#include "Utils/Arena/LockingPolicy.h"
#include "Utils/Compiler.h"
#include "Utils/Log.h"
#include "Utils/string/ImmutableString.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>

BEGIN_NS_BACKEND

class DebugTag {
public:
    DebugTag();

    void                   WritePoolHandleTag(HandleBase::HandleId key, utils::ImmutableString&& tag) noexcept;
    void                   WriteHeapHandleTag(HandleBase::HandleId key, utils::ImmutableString&& tag) noexcept;
    utils::ImmutableString FindHandleTag(HandleBase::HandleId key) const noexcept;

private:
    // mDebugTags 仅在主驱动线程写入，但 handle_cast 同步调用可能跨线程读
    mutable utils::LockingPolicy::Mutex                                         mDebugTagLock;
    std::unordered_map<HandleBase::HandleId, utils::ImmutableString> mDebugTags UTILS_GUARDED_BY(mDebugTagLock);
};

template <size_t P0, size_t P1, size_t P2>
class HandleAllocator : public DebugTag {
public:
    HandleAllocator(const char* name, size_t size);
    HandleAllocator(const char* name, size_t size, bool disableUseAfterFreeCheck, bool disableHeapHandleTags);
    HandleAllocator(HandleAllocator const& rhs)            = delete;
    HandleAllocator& operator=(HandleAllocator const& rhs) = delete;
    ~HandleAllocator() noexcept;

    template <typename D, typename... ARGS>
    Handle<D> AllocateAndConstruct(ARGS&&... args) {
        Handle<D> h{ AllocateHandle<D>() };
        D*        addr = HandleCast<D*>(h);
        new (addr) D(std::forward<ARGS>(args)...);
        return h;
    }

    template <typename D>
    Handle<D> Allocate() {
        Handle<D> h{ AllocateHandle<D>() };
        return h;
    }

    template <typename D, typename B, typename... ARGS>
    std::enable_if_t<std::is_base_of_v<B, D>, D>* DestroyAndConstruct(Handle<B> const& handle, ARGS&&... args) {
        LOG_ASSERT(handle);
        D* addr = HandleCast<D*>(const_cast<Handle<B>&>(handle));
        LOG_ASSERT(addr);
        // 以 dtor+ctor 实现重建；析构均为平凡，~D() 实为 noop
        addr->~D();
        new (addr) D(std::forward<ARGS>(args)...);
        return addr;
    }

    template <typename D, typename B, typename... ARGS>
    std::enable_if_t<std::is_base_of_v<B, D>, D>* Construct(Handle<B> const& handle, ARGS&&... args) noexcept {
        LOG_ASSERT(handle);
        D* addr = HandleCast<D*>(const_cast<Handle<B>&>(handle));
        LOG_ASSERT(addr);
        new (addr) D(std::forward<ARGS>(args)...);
        return addr;
    }

    template <typename B, typename D, typename = std::enable_if_t<std::is_base_of_v<B, D>, D>>
    void Deallocate(Handle<B>& handle, D const* p) noexcept {
        // 与 operator delete 一致，允许释放 nullptr
        if (p) {
            p->~D();
            DeallocateHandle<D>(handle.GetId());
        }
    }

    template <typename D, typename B, typename = std::enable_if_t<std::is_base_of_v<B, D>, D>>
    void Deallocate(Handle<B>& handle) noexcept {
        D const* d = HandleCast<const D*>(handle);
        Deallocate(handle, d);
    }

    template <typename Dp, typename B>
    inline std::enable_if_t<std::is_pointer_v<Dp> && std::is_base_of_v<B, std::remove_pointer_t<Dp>>, Dp> HandleCast(
        Handle<B>& handle) {
        LOG_ASSERT(handle);
        return static_cast<Dp>(HandleCast(handle.GetId()));
    }

    template <typename Dp, typename B>
    inline std::enable_if_t<std::is_pointer_v<Dp> && std::is_base_of_v<B, std::remove_pointer_t<Dp>>, Dp> HandleCast(
        Handle<B> const& handle) {
        return HandleCast<Dp>(const_cast<Handle<B>&>(handle));
    }

    utils::ImmutableString GetHandleTag(HandleBase::HandleId key) const noexcept;

    template <typename B>
    bool IsValid(Handle<B>& handle) {
        if (!handle) {
            return false;
        }
        auto [p, tag] = HandleToPointer(handle.GetId());
        if (IsPoolHandle(handle.GetId())) {
            uint8_t const age         = (tag & HANDLE_AGE_MASK) >> HANDLE_AGE_SHIFT;
            auto const    pNode       = static_cast<typename Allocator::Node*>(p);
            uint8_t const expectedAge = pNode[-1].age;
            return expectedAge == age;
        }
        return p != nullptr;
    }

    void AssociateTagToHandle(HandleBase::HandleId id, utils::ImmutableString&& tag) noexcept;

private:
    template <typename D>
    static constexpr size_t GetBucketSize() noexcept {
        if constexpr (sizeof(D) <= P0) {
            return P0;
        }
        if constexpr (sizeof(D) <= P1) {
            return P1;
        }
        static_assert(sizeof(D) <= P2);
        return P2;
    }

    class Allocator : public utils::AllocatorPolicyBase {
        friend class HandleAllocator;

        static constexpr size_t MIN_ALIGNMENT = alignof(std::max_align_t);
        struct Node {
            uint8_t age;
        };

        template <size_t SIZE>
        using Pool = utils::PoolAllocator<SIZE, MIN_ALIGNMENT, sizeof(Node)>;

        Pool<P0>                                mPool0;
        Pool<P1>                                mPool1;
        Pool<P2>                                mPool2;
        [[maybe_unused]] const utils::HeapArea& mArea;
        bool                                    mUseAfterFreeCheckDisabled;

        utils::AllocatorPolicyBase* selectPool(size_t size) noexcept {
            if (size <= mPool0.GetSize())
                return &mPool0;
            else if (size <= mPool1.GetSize())
                return &mPool1;
            else if (size <= mPool2.GetSize())
                return &mPool2;
            return nullptr;
        }

    public:
        explicit Allocator(const utils::HeapArea& area, bool disableUseAfterFreeCheck);

        static constexpr size_t GetAlignment() noexcept { return MIN_ALIGNMENT; }

        // 基类 2 参纯虚交集；3 参路径由 Arena 直调
        NODISCARD void* Alloc(size_t size, size_t alignment) noexcept override { return Alloc(size, alignment, 0); }

        NODISCARD void* Alloc(size_t size, size_t alignment, size_t extra) noexcept {
            utils::AllocatorPolicyBase* const pool = selectPool(size);
            return pool ? pool->Alloc(size, alignment) : nullptr;
        }

        // age 校验与递增已由 HandleAllocator 层完成，这里只归还池块
        void Free(void* p, size_t size) noexcept override {
            LOG_ASSERT(p >= mArea.GetBegin() && static_cast<char*>(p) + size <= static_cast<char*>(mArea.GetEnd()));

            utils::AllocatorPolicyBase* const pool = selectPool(size);
            LOG_ASSERT(pool != nullptr);
            if (pool) {
                pool->Free(p, size);
            }
        }
    };

    using HandleArena = utils::Arena<Allocator, utils::LockingPolicy::Mutex>;

    template <typename D>
    HandleBase::HandleId AllocateHandle() {
        constexpr size_t BUCKET_SIZE = GetBucketSize<D>();
        return AllocateHandleInPool<BUCKET_SIZE>();
    }

    template <typename D>
    void DeallocateHandle(HandleBase::HandleId id) noexcept {
        constexpr size_t BUCKET_SIZE = GetBucketSize<D>();
        DeallocateHandleFromPool<BUCKET_SIZE>(id);
    }

    // 非内联：arena 加锁使代码不平凡，按池仅生成三个版本
    template <size_t SIZE>
    UTILS_NOINLINE HandleBase::HandleId AllocateHandleInPool() {
        void* p = mHandleArena.Alloc(SIZE, Allocator::GetAlignment(), 0);
        if (UTILS_LIKELY(p)) {
            auto const     pNode = static_cast<typename Allocator::Node*>(p);
            uint8_t const  age   = pNode[-1].age;
            uint32_t const tag   = (static_cast<uint32_t>(age) << HANDLE_AGE_SHIFT) & HANDLE_AGE_MASK;
            return ArenaPointerToHandle(p, tag);
        }
        return AllocateHandleSlow(SIZE);
    }

    template <size_t SIZE>
    UTILS_NOINLINE void DeallocateHandleFromPool(HandleBase::HandleId id) noexcept {
        if (UTILS_LIKELY(IsPoolHandle(id))) {
            auto [p, tag]     = HandleToPointer(id);
            uint8_t const age = (tag & HANDLE_AGE_MASK) >> HANDLE_AGE_SHIFT;
            // 期望 age 与内存 age 不一致即 double-free
            auto const pNode       = static_cast<typename Allocator::Node*>(p);
            uint8_t&   expectedAge = pNode[-1].age;
            if (UTILS_UNLIKELY(!mUseAfterFreeCheckDisabled && expectedAge != age)) {
                LOG_CRITICAL("double-free of Handle of size {} with id={}", SIZE, id);
            }
            expectedAge = (expectedAge + 1) & 0xF;
            mHandleArena.Free(p, SIZE);
        } else {
            DeallocateHandleSlow(id, SIZE);
        }
    }

    // 句柄 age 位段：4 位，其中低 2 位为调试标签
    static constexpr uint32_t HANDLE_AGE_BIT_COUNT       = 4;
    static constexpr uint32_t HANDLE_DEBUG_TAG_BIT_COUNT = 2;
    static constexpr uint32_t HANDLE_AGE_SHIFT           = 27;
    static constexpr uint32_t HANDLE_HEAP_FLAG           = 0x80000000u;
    static constexpr uint32_t HANDLE_AGE_MASK            = ((1 << HANDLE_AGE_BIT_COUNT) - 1) << HANDLE_AGE_SHIFT;
    static constexpr uint32_t HANDLE_DEBUG_TAG_MASK      = ((1 << HANDLE_DEBUG_TAG_BIT_COUNT) - 1) << HANDLE_AGE_SHIFT;
    static constexpr uint32_t HANDLE_INDEX_MASK          = 0x07FFFFFFu;
    // 截断 age 高 2 位，仅保留低 2 位（调试标签位段）作为标签键
    static constexpr uint32_t HANDLE_TAG_KEY_MASK = ~(HANDLE_DEBUG_TAG_MASK ^ HANDLE_AGE_MASK);

    static_assert(HANDLE_DEBUG_TAG_BIT_COUNT <= HANDLE_AGE_BIT_COUNT);

    static bool IsPoolHandle(HandleBase::HandleId id) noexcept { return (id & HANDLE_HEAP_FLAG) == 0u; }

    HandleBase::HandleId AllocateHandleSlow(size_t size);
    void                 DeallocateHandleSlow(HandleBase::HandleId id, size_t size) noexcept;

    // 快路径仅 4 条指令，保持内联
    std::pair<void*, uint32_t> HandleToPointer(HandleBase::HandleId id) const noexcept {
        if (UTILS_LIKELY(IsPoolHandle(id))) {
            char* const    base   = static_cast<char*>(mHandleArena.GetArea().GetBegin());
            uint32_t const tag    = id & HANDLE_AGE_MASK;
            size_t const   offset = (id & HANDLE_INDEX_MASK) * Allocator::GetAlignment();
            return { static_cast<void*>(base + offset), tag };
        }
        return { HandleToPointerSlow(id), 0 };
    }

    void* HandleToPointerSlow(HandleBase::HandleId id) const noexcept;
    void* HandleCast(HandleBase::HandleId id) const;

    // 快路径仅 3 条指令，保持内联
    HandleBase::HandleId ArenaPointerToHandle(void* p, uint32_t tag) const noexcept {
        char* const  base   = static_cast<char*>(mHandleArena.GetArea().GetBegin());
        size_t const offset = static_cast<char*>(p) - base;
        LOG_ASSERT((offset % Allocator::GetAlignment()) == 0);
        auto id = HandleBase::HandleId(offset / Allocator::GetAlignment());
        id |= tag & HANDLE_AGE_MASK;
        LOG_ASSERT((id & HANDLE_HEAP_FLAG) == 0);
        return id;
    }

    HandleArena mHandleArena;

    // 仅当 arena 耗尽走系统堆时使用
    mutable utils::LockingPolicy::Mutex                          mLock;
    std::unordered_map<HandleBase::HandleId, void*> mOverflowMap UTILS_GUARDED_BY(mLock);
    std::atomic<HandleBase::HandleId>                            mId = 0;

    const bool mUseAfterFreeCheckDisabled;
    const bool mHeapHandleTagsDisabled;
};

using HandleAllocatorVK = HandleAllocator<64, 160, 312>;

END_NS_BACKEND
