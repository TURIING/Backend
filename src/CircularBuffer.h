#pragma once

#include "Backend/DriverDefine.h"

#include <cstddef>

BEGIN_NS_BACKEND

/**
 * @brief 环形内存缓冲区
 *
 * 采用"软环形"实现：底层是一段 2×size 的连续匿名内存映射，命令流按线性地址持续写入，
 * 越过逻辑末尾后继续写入第二段物理内存。getBuffer() 以滑动窗口方式回收已写入区域，
 * 上层无需关心地址回绕。
 *
 * 写入端必须是单线程（由 CommandStream 保证），读取端由 CommandBufferQueue 控制流。
 *
 * 继承 Ref 以对齐项目对象模型，可用 CircularBufferPtr 引用计数持有。
 */
class CircularBuffer : public NS_UTILS::Ref {
public:
    /**
     * @brief 构造环形缓冲区
     * @param bufferSize 逻辑缓冲区大小；建议设为 ≥ 3×requiredSize，
     *                   否则记录线程可能因空间不足而阻塞
     */
    explicit CircularBuffer(size_t bufferSize);

    // 禁止拷贝与移动
    CircularBuffer(CircularBuffer const& rhs)                = delete;
    CircularBuffer(CircularBuffer&& rhs) noexcept            = delete;
    CircularBuffer& operator=(CircularBuffer const& rhs)     = delete;
    CircularBuffer& operator=(CircularBuffer&& rhs) noexcept = delete;

    ~CircularBuffer() noexcept override;

    /** @brief 系统页大小，即地址映射的对齐粒度 */
    static size_t getBlockSize() noexcept { return sPageSize; }

    /** @brief 环形缓冲区总大小（常量） */
    size_t size() const noexcept { return m_size; }

    /**
     * @brief 在缓冲区中分配 s 字节并返回指针
     * @param s 分配大小，单次分配不得超过 size() 字节
     */
    inline void* allocate(size_t s) noexcept {
        LOG_ASSERT(getUsed() + s <= size());
        char* const cur = static_cast<char*>(m_head);
        m_head           = cur + s;
        return cur;
    }

    /** @brief 缓冲区是否为空（自上次 getBuffer() 后无任何写入） */
    bool empty() const noexcept { return m_tail == m_head; }

    /** @brief 自上次 getBuffer() 以来的已用字节数 */
    size_t getUsed() const noexcept { return static_cast<size_t>(intptr_t(m_head) - intptr_t(m_tail)); }

    /** @brief 已写入的连续数据范围 */
    struct Range {
        void* tail;
        void* head;
    };

    /**
     * @brief 取出当前已写入的数据范围并回收
     *
     * 调用方必须保证：在下次 allocate() 累计分配满 (size() - getUsed()) 字节之前，
     * 该范围内数据已不再被使用。
     */
    Range getBuffer() noexcept;

private:
    // 分配底层 2×size 内存（平台相关：mmap / VirtualAlloc / malloc）
    void* alloc(size_t size);
    // 释放底层内存
    void  dealloc() noexcept;

    // 环形缓冲区起始地址（常量）
    void* m_data = nullptr;

    // 环形缓冲区逻辑大小（常量）
    size_t const m_size;

    // 已记录数据的起始指针
    void* m_tail = nullptr;

    // 下一个可用命令的写入指针
    void* m_head = nullptr;

    // 系统页大小
    static size_t sPageSize;
};

DECLARE_SHARE_PTR_CLASS(CircularBuffer);

END_NS_BACKEND
