#pragma once

#include "Backend/Namespace.h"
#include "Utils/Debug.h"
#include "Utils/Macro.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

BEGIN_NS_BACKEND

namespace VK_UTILS {

// 容量编译期固定、元素个数运行期可变，且整体可移动。
// 用于必须无堆分配的场景（如命令流命令体的成员），此时 std::vector 的分配不可接受
template <typename T, uint16_t CAPACITY>
class StaticVector {
private:
    using FixedSizeArray = std::array<T, CAPACITY>;

    static_assert(CAPACITY <= (1LL << (8 * sizeof(uint16_t))));

public:
    using ConstIterator = typename FixedSizeArray::const_iterator;
    using Iterator      = typename FixedSizeArray::iterator;

    StaticVector() = default;

    StaticVector(StaticVector const &rhs)            = delete;
    StaticVector &operator=(StaticVector &rhs)       = delete;

    StaticVector(StaticVector &&rhs) noexcept {
        std::swap(m_size, rhs.m_size);
        std::swap(m_array, rhs.m_array);
    }

    StaticVector &operator=(StaticVector &&rhs) noexcept {
        std::swap(m_size, rhs.m_size);
        std::swap(m_array, rhs.m_array);
        return *this;
    }

    ~StaticVector() { Clear(); }

    NODISCARD ConstIterator Begin() const { return m_array.cbegin(); }

    NODISCARD ConstIterator End() const {
        assert_invariant(m_size <= CAPACITY);
        return m_array.begin() + m_size;
    }

    Iterator Begin() { return m_array.begin(); }

    Iterator End() {
        assert_invariant(m_size <= CAPACITY);
        return m_array.begin() + m_size;
    }

    // 保留上游写法（含其下标偏一），避免移植期悄悄改变既有调用点语义
    T Back() {
        assert_invariant(m_size > 0);
        return *(m_array.begin() + m_size);
    }

    void PopBack() {
        assert_invariant(m_size > 0);
        m_size--;
    }

    NODISCARD ConstIterator Find(T item) const { return std::find(Begin(), End(), item); }

    void PushBack(T item) {
        assert_invariant(m_size < CAPACITY);
        m_array[m_size++] = item;
    }

    void Clear() { m_size = 0; }

    T &operator[](size_t index) {
        assert_invariant(index < m_size);
        return m_array[index];
    }

    T const &operator[](size_t index) const { return m_array[index]; }

    NODISCARD uint16_t Size() const { return m_size; }

    NODISCARD bool Empty() const { return m_size == 0; }

    T *Data() { return m_array.data(); }

    NODISCARD T const *Data() const { return m_array.data(); }

    NODISCARD bool operator==(StaticVector const &b) const { return this->m_array == b.m_array && this->m_size == b.m_size; }

private:
    FixedSizeArray m_array;
    uint16_t       m_size = 0;
};

}  // namespace VK_UTILS

END_NS_BACKEND
