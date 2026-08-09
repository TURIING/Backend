#pragma once

#include "Backend/DriverDefine.h"

#include <cstddef>

BEGIN_NS_BACKEND

// 环形内存缓冲区："软环形"实现，底层是一段 2×size 的连续匿名内存映射，
// 命令流按线性地址持续写入，越过逻辑末尾后写入第二段物理内存；getBuffer() 滑动回收，
// 上层无需关心地址回绕。写入端须单线程（CommandStream 保证），读取端由 CommandBufferQueue 控制。
class CircularBuffer : public NS_UTILS::Ref {
public:
    // bufferSize 建议 ≥ 3×requiredSize，否则记录线程可能因空间不足而阻塞
    explicit CircularBuffer(size_t bufferSize);

    // 禁止拷贝与移动
    CircularBuffer(CircularBuffer const& rhs)                = delete;
    CircularBuffer(CircularBuffer&& rhs) noexcept            = delete;
    CircularBuffer& operator=(CircularBuffer const& rhs)     = delete;
    CircularBuffer& operator=(CircularBuffer&& rhs) noexcept = delete;

    ~CircularBuffer() noexcept override;

    static size_t getBlockSize() noexcept { return sPageSize; }

    size_t size() const noexcept { return m_size; }

    inline void* allocate(size_t s) noexcept {
        LOG_ASSERT(getUsed() + s <= size());
        char* const cur = static_cast<char*>(m_head);
        m_head           = cur + s;
        return cur;
    }

    bool empty() const noexcept { return m_tail == m_head; }

    size_t getUsed() const noexcept { return static_cast<size_t>(intptr_t(m_head) - intptr_t(m_tail)); }

    // 已写入的连续数据范围
    struct Range {
        void* tail;
        void* head;
    };

    // 调用方须保证：在下次 allocate() 累计分配满 (size() - getUsed()) 字节前，返回范围内数据已不再被使用
    Range getBuffer() noexcept;

private:
    // 分配底层 2×size 内存（平台相关：mmap / VirtualAlloc / malloc）
    void* alloc(size_t size);
    void  dealloc() noexcept;

    void* m_data = nullptr;

    size_t const m_size;

    // tail：已记录数据的起始；head：下一个可用命令的写入位置
    void* m_tail = nullptr;
    void* m_head = nullptr;

    static size_t sPageSize;
};

DECLARE_SHARE_PTR_CLASS(CircularBuffer);

END_NS_BACKEND
