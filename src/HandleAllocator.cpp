#include "HandleAllocator.h"

#include "Utils/thread/lock/LockGuard.h"
#include "Utils/thread/lock/UniqueLock.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <utility>

BEGIN_NS_BACKEND

template <size_t P0, size_t P1, size_t P2>
UTILS_NOINLINE
HandleAllocator<P0, P1, P2>::Allocator::Allocator(utils::HeapArea const& area, bool disableUseAfterFreeCheck)
        : mArea(area),
          mUseAfterFreeCheckDisabled(disableUseAfterFreeCheck) {
    // 句柄索引受 HANDLE_INDEX_MASK 位段限制，超出时压缩 arena；清零保证 age 初始为 0
    size_t const maxHeapSize = std::min(area.GetSize(), static_cast<size_t>(HANDLE_INDEX_MASK) * GetAlignment());
    if (UTILS_UNLIKELY(maxHeapSize != area.GetSize())) {
        LOG_WARN("HandleAllocator heap size reduced to {} from {}", maxHeapSize, area.GetSize());
    }

    memset(area.GetData(), 0, maxHeapSize);

    // 三池均分 arena，使各池可容纳相同数量的句柄
    size_t const count = maxHeapSize / (P0 + P1 + P2);
    char* const p0 = static_cast<char*>(area.GetBegin());
    char* const p1 = p0 + count * P0;
    char* const p2 = p1 + count * P1;

    mPool0 = Pool<P0>(p0, count * P0);
    mPool1 = Pool<P1>(p1, count * P1);
    mPool2 = Pool<P2>(p2, count * P2);
}

// name 仅用于与 Filament 构造签名保持一致，便于未来对照迁移调用点
template <size_t P0, size_t P1, size_t P2>
HandleAllocator<P0, P1, P2>::HandleAllocator(const char*, size_t size,
        bool disableUseAfterFreeCheck, bool disableHeapHandleTags)
    : mHandleArena(size, disableUseAfterFreeCheck),
      mUseAfterFreeCheckDisabled(disableUseAfterFreeCheck),
      mHeapHandleTagsDisabled(disableHeapHandleTags) {
}

template <size_t P0, size_t P1, size_t P2>
HandleAllocator<P0, P1, P2>::HandleAllocator(const char* name, size_t size)
    : HandleAllocator(name, size, false, false) {
}

template <size_t P0, size_t P1, size_t P2>
HandleAllocator<P0, P1, P2>::~HandleAllocator() noexcept {
    auto& overflowMap = mOverflowMap;
    if (!overflowMap.empty()) {
        LOG_ERROR("Not all handles have been freed. Probably leaking memory.");
        for (auto& entry : overflowMap) {
            ::free(entry.second);
        }
    }
}

template <size_t P0, size_t P1, size_t P2>
UTILS_NOINLINE
void* HandleAllocator<P0, P1, P2>::HandleToPointerSlow(HandleBase::HandleId id) const noexcept {
    auto& overflowMap = mOverflowMap;
    utils::LockGuard const lock(mLock);
    auto pos = overflowMap.find(id);
    if (pos != overflowMap.end()) {
        return pos->second;
    }
    return nullptr;
}

template<size_t P0, size_t P1, size_t P2>
UTILS_NOINLINE
void* HandleAllocator<P0, P1, P2>::HandleCast(HandleBase::HandleId id) const {
    auto [p, tag] = HandleToPointer(id);
    if (IsPoolHandle(id)) {
        if (UTILS_UNLIKELY(!mUseAfterFreeCheckDisabled)) {
            uint8_t const age = (tag & HANDLE_AGE_MASK) >> HANDLE_AGE_SHIFT;
            auto const pNode = static_cast<typename Allocator::Node*>(p);
            uint8_t const expectedAge = pNode[-1].age;
            // 内存 age 与句柄内 age 不一致即 use-after-free
            if (UTILS_UNLIKELY(expectedAge != age)) {
                LOG_CRITICAL("use-after-free of Handle with id={}, tag={}", id, GetHandleTag(id).c_str_safe());
            }
        }
    } else {
        if (UTILS_UNLIKELY(!mUseAfterFreeCheckDisabled)) {
            HandleBase::HandleId const index = (id & HANDLE_INDEX_MASK);
            // 已发过同索引句柄则为 use-after-free，否则多半是句柄损坏
            if (index < mId.load(std::memory_order_relaxed)) {
                if (UTILS_UNLIKELY(p == nullptr)) {
                    LOG_CRITICAL("use-after-free of heap Handle with id={}, tag={}", id, GetHandleTag(id).c_str_safe());
                }
            } else {
                if (UTILS_UNLIKELY(p == nullptr)) {
                    LOG_CRITICAL("corrupted heap Handle with id={}, tag={}", id, GetHandleTag(id).c_str_safe());
                }
            }
        }
    }
    return p;
}

template <size_t P0, size_t P1, size_t P2>
HandleBase::HandleId HandleAllocator<P0, P1, P2>::AllocateHandleSlow(size_t size) {
    void* p = ::malloc(size);

    auto const nextId = mId.fetch_add(1, std::memory_order_relaxed) + 1;
    if (UTILS_UNLIKELY(nextId >= HANDLE_HEAP_FLAG)) {
        LOG_CRITICAL("No more Handle ids available! This can happen if HandleAllocator arena has been full "
                     "for a while. Please increase the arena size constant.");
    }

    HandleBase::HandleId id = nextId | HANDLE_HEAP_FLAG;

    utils::UniqueLock lock(mLock);
    mOverflowMap.emplace(id, p);
    lock.unlock();

    // 首个堆句柄说明 arena 已满，仅提示一次
    if (UTILS_UNLIKELY(id == (HANDLE_HEAP_FLAG | 1u))) {
        LOG_ERROR("HandleAllocator arena is full, using slower system heap. Please increase the appropriate constant.");
    }
    return id;
}

template <size_t P0, size_t P1, size_t P2>
void HandleAllocator<P0, P1, P2>::DeallocateHandleSlow(HandleBase::HandleId id, size_t) noexcept {
    LOG_ASSERT(id & HANDLE_HEAP_FLAG);
    void* p = nullptr;
    auto& overflowMap = mOverflowMap;

    utils::UniqueLock lock(mLock);
    auto pos = overflowMap.find(id);
    if (pos != overflowMap.end()) {
        p = pos->second;
        overflowMap.erase(pos);
    }
    lock.unlock();

    ::free(p);
}

template<size_t P0, size_t P1, size_t P2>
UTILS_NOINLINE
utils::ImmutableString HandleAllocator<P0, P1, P2>::GetHandleTag(HandleBase::HandleId id) const noexcept {
    uint32_t key = id;
    if (UTILS_LIKELY(IsPoolHandle(id))) {
        key &= HANDLE_TAG_KEY_MASK;
    }
    return FindHandleTag(key);
}

template<size_t P0, size_t P1, size_t P2>
void HandleAllocator<P0, P1, P2>::AssociateTagToHandle(HandleBase::HandleId id, utils::ImmutableString&& tag) noexcept {
    if (tag.empty()) {
        return;
    }
    uint32_t key = id;
    if (UTILS_LIKELY(IsPoolHandle(id))) {
        key &= HANDLE_TAG_KEY_MASK;
        WritePoolHandleTag(key, std::move(tag));
    } else {
        if (!mHeapHandleTagsDisabled) {
            WriteHeapHandleTag(key, std::move(tag));
        }
    }
}

DebugTag::DebugTag() {
    // 预分配避免首批标签频繁 malloc
    mDebugTags.reserve(512);
}

UTILS_NOINLINE
utils::ImmutableString DebugTag::FindHandleTag(HandleBase::HandleId key) const noexcept {
    utils::LockGuard const lock(mDebugTagLock);
    if (auto pos = mDebugTags.find(key); pos != mDebugTags.end()) {
        return pos->second;
    }
    return "(no tag)";
}

UTILS_NOINLINE
void DebugTag::WritePoolHandleTag(HandleBase::HandleId key, utils::ImmutableString&& tag) noexcept {
    utils::LockGuard const lock(mDebugTagLock);
    mDebugTags[key] = std::move(tag);
}

UTILS_NOINLINE
void DebugTag::WriteHeapHandleTag(HandleBase::HandleId key, utils::ImmutableString&& tag) noexcept {
    utils::LockGuard const lock(mDebugTagLock);
    // 堆句柄标签不回收，慢路径下可能持续增长
    mDebugTags[key] = std::move(tag);
}

template class HandleAllocator<64, 160, 312>;

END_NS_BACKEND
