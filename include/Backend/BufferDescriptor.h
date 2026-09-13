#pragma once

#include "Backend/Namespace.h"

#include "Utils/Macro.h"

#include <cstddef>

BEGIN_NS_BACKEND

class CallbackHandler;

/**
 * CPU 侧内存块描述符，典型用途是把数据从 CPU 传到 GPU。
 *
 * BufferDescriptor 拥有所引用的内存块，故不可拷贝、只可移动；析构时释放对内存块的所有权。
 */
class BufferDescriptor {
public:
    /**
     * 释放缓冲区数据的回调。
     *
     * 保证：在主线程调用。
     * 限制：必须轻量，且不得回调任何后端 API。
     */
    using Callback = void (*)(void* buffer, size_t size, void* user);

    BufferDescriptor() noexcept = default;

    // 通知调用方 BufferDescriptor 不再拥有该缓冲区
    ~BufferDescriptor() noexcept {
        if (mCallback) {
            mCallback(buffer, size, mUser);
        }
    }

    BufferDescriptor(BufferDescriptor const& rhs)            = delete;
    BufferDescriptor& operator=(BufferDescriptor const& rhs) = delete;

    BufferDescriptor(BufferDescriptor&& rhs) noexcept
            : buffer(rhs.buffer), size(rhs.size),
              mCallback(rhs.mCallback), mUser(rhs.mUser), mHandler(rhs.mHandler) {
        rhs.buffer    = nullptr;
        rhs.mCallback = nullptr;
    }

    BufferDescriptor& operator=(BufferDescriptor&& rhs) noexcept {
        if (this != &rhs) {
            buffer    = rhs.buffer;
            size      = rhs.size;
            mCallback = rhs.mCallback;
            mUser     = rhs.mUser;
            mHandler  = rhs.mHandler;

            // 源对象交回所有权后不得再触发回调
            rhs.buffer    = nullptr;
            rhs.mCallback = nullptr;
        }
        return *this;
    }

    BufferDescriptor(void const* buffer, size_t const size,
            Callback const callback = nullptr, void* user = nullptr) noexcept
            : buffer(const_cast<void*>(buffer)), size(size), mCallback(callback), mUser(user) {}

    BufferDescriptor(void const* buffer, size_t const size,
            CallbackHandler* handler, Callback const callback, void* user = nullptr) noexcept
            : buffer(const_cast<void*>(buffer)), size(size),
              mCallback(callback), mUser(user), mHandler(handler) {}

    void SetCallback(Callback const callback, void* user = nullptr) noexcept {
        mCallback = callback;
        mUser     = user;
        mHandler  = nullptr;
    }

    void SetCallback(CallbackHandler* handler, Callback const callback, void* user = nullptr) noexcept {
        mCallback = callback;
        mUser     = user;
        mHandler  = handler;
    }

    NODISCARD bool HasCallback() const noexcept { return mCallback != nullptr; }

    NODISCARD Callback GetCallback() const noexcept { return mCallback; }

    //! 返回派发回调所用的 handler；为 nullptr 表示使用默认 handler
    NODISCARD CallbackHandler* GetHandler() const noexcept { return mHandler; }

    //! 返回登记的用户指针
    NODISCARD void* GetUser() const noexcept { return mUser; }

    //! CPU 内存块虚拟地址
    void* buffer = nullptr;

    //! CPU 内存块字节数
    size_t size = 0;

private:
    Callback         mCallback = nullptr;
    void*            mUser     = nullptr;
    CallbackHandler* mHandler  = nullptr;
};

END_NS_BACKEND
