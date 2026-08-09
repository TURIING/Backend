#include "CircularBuffer.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

using namespace Backend;

namespace {

void fillPattern(char* const begin, size_t const count, uint8_t const seed) {
    for (size_t i = 0; i < count; ++i) {
        begin[i] = static_cast<char>(static_cast<uint8_t>(seed + i));
    }
}

// 校验 [begin, begin + count) 字节与 fillPattern(seed) 写入的模式一致
bool checkPattern(char const* const begin, size_t const count, uint8_t const seed) {
    for (size_t i = 0; i < count; ++i) {
        if (static_cast<uint8_t>(begin[i]) != static_cast<uint8_t>(seed + i)) {
            return false;
        }
    }
    return true;
}

} // namespace

// ============================================================================
// 初始状态
// ============================================================================

// 构造后应处于空状态：未写入、未使用、大小等于构造参数
TEST(CircularBufferTest, FreshBufferIsEmpty)
{
    constexpr size_t kSize = 256;
    CircularBuffer buffer(kSize);

    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.getUsed(), 0u);
    EXPECT_EQ(buffer.size(), kSize);
    EXPECT_GT(buffer.getBlockSize(), 0u);
}

// ============================================================================
// 基本分配
// ============================================================================

// allocate(s) 后使用量增加 s，缓冲区不再为空，返回非空指针
TEST(CircularBufferTest, AllocateIncreasesUsed)
{
    constexpr size_t kSize = 256;
    CircularBuffer buffer(kSize);

    void* const ptr = buffer.allocate(16);
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(buffer.getUsed(), 16u);
    EXPECT_FALSE(buffer.empty());
}

// ============================================================================
// 数据完整性
// ============================================================================

// 写入的字节序列应能被 getBuffer() 返回的 Range 完整读回，范围精确
TEST(CircularBufferTest, WriteThenReadBack)
{
    constexpr size_t kSize = 256;
    CircularBuffer buffer(kSize);

    char* const data = static_cast<char*>(buffer.allocate(64));
    fillPattern(data, 64, 0x10);

    CircularBuffer::Range const range = buffer.getBuffer();
    EXPECT_EQ(range.tail, data);
    EXPECT_EQ(static_cast<char const*>(range.head), data + 64);
    EXPECT_TRUE(checkPattern(static_cast<char const*>(range.tail), 64, 0x10));
}

// ============================================================================
// 回收复位
// ============================================================================

// getBuffer() 回收已写入区域后回到空状态，且可继续写入不冲突
TEST(CircularBufferTest, GetBufferResetsAndAllowsRewriting)
{
    constexpr size_t kSize = 256;
    CircularBuffer buffer(kSize);

    char* const first = static_cast<char*>(buffer.allocate(32));
    fillPattern(first, 32, 0x11);
    buffer.getBuffer();

    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.getUsed(), 0u);

    char* const second = static_cast<char*>(buffer.allocate(32));
    fillPattern(second, 32, 0x22);
    CircularBuffer::Range const range = buffer.getBuffer();
    EXPECT_EQ(static_cast<char const*>(range.head), second + 32);
    EXPECT_TRUE(checkPattern(static_cast<char const*>(range.tail), 32, 0x22));
}

// ============================================================================
// 软环形回绕（核心语义）
// ============================================================================

// 软环形核心：第二轮写入越过逻辑末尾 m_data + size()，落入第二段物理内存，
// getBuffer() 仍返回单一连续 Range，数据完整读回，随后 head 回绕复位到起点。
//
// 注意：单轮内 allocate 受 getUsed() + s <= size() 约束，head 不会越过逻辑末尾；
// 必须两轮——第一轮让 tail 停在非零位置，第二轮再写满，head 才跨入第二段物理内存。
TEST(CircularBufferTest, WriteAcrossLogicalBoundary)
{
    constexpr size_t kSize = 256;
    CircularBuffer buffer(kSize);

    // 第一轮：从起点写入 192 字节，getBuffer 后 tail 停在 data + 192
    char* const first = static_cast<char*>(buffer.allocate(192));
    fillPattern(first, 192, 0x01);
    buffer.getBuffer();

    // 第二轮：再分配 128 字节，head 越过逻辑末尾 data + 256，跨入第二段物理内存
    char* const second = static_cast<char*>(buffer.allocate(128));
    EXPECT_EQ(second, first + 192);
    fillPattern(second, 128, 0x02);
    EXPECT_EQ(buffer.getUsed(), 128u);

    CircularBuffer::Range const range = buffer.getBuffer();
    EXPECT_EQ(range.tail, second);
    EXPECT_EQ(static_cast<char const*>(range.head), second + 128);
    EXPECT_TRUE(checkPattern(static_cast<char const*>(range.tail), 128, 0x02));

    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.getUsed(), 0u);
}

// ============================================================================
// 多轮复用
// ============================================================================

// 写 → getBuffer → 校验反复多轮，head 每轮正确推进，末轮越过边界后复位
TEST(CircularBufferTest, MultipleCycles)
{
    constexpr size_t kSize = 256;
    CircularBuffer buffer(kSize);

    for (uint8_t round = 0; round < 4; ++round) {
        char* const data = static_cast<char*>(buffer.allocate(64));
        fillPattern(data, 64, static_cast<uint8_t>(round + 1));

        CircularBuffer::Range const range = buffer.getBuffer();
        EXPECT_EQ(static_cast<char const*>(range.head), data + 64);
        EXPECT_TRUE(checkPattern(static_cast<char const*>(range.tail), 64, static_cast<uint8_t>(round + 1)));
        EXPECT_TRUE(buffer.empty());
        EXPECT_EQ(buffer.getUsed(), 0u);
    }
}

// ============================================================================
// 顶格分配
// ============================================================================

// 单次恰好分配 size() 字节，缓冲区满，数据可完整读回
TEST(CircularBufferTest, SingleMaxAllocation)
{
    constexpr size_t kSize = 256;
    CircularBuffer buffer(kSize);

    char* const data = static_cast<char*>(buffer.allocate(kSize));
    fillPattern(data, kSize, 0xAA);
    EXPECT_EQ(buffer.getUsed(), kSize);

    CircularBuffer::Range const range = buffer.getBuffer();
    EXPECT_EQ(static_cast<char const*>(range.head), data + kSize);
    EXPECT_TRUE(checkPattern(static_cast<char const*>(range.tail), kSize, 0xAA));
    EXPECT_TRUE(buffer.empty());
}

// 多次小分配累计恰满 size() 字节，分配连续紧贴，不触发断言
TEST(CircularBufferTest, CumulativeMaxAllocation)
{
    constexpr size_t kSize = 256;
    CircularBuffer buffer(kSize);

    char* const first  = static_cast<char*>(buffer.allocate(100));
    char* const second = static_cast<char*>(buffer.allocate(100));
    char* const third  = static_cast<char*>(buffer.allocate(56));

    EXPECT_EQ(second, first + 100);
    EXPECT_EQ(third, first + 200);
    EXPECT_EQ(buffer.getUsed(), kSize);

    buffer.getBuffer();
    EXPECT_TRUE(buffer.empty());
}

// ============================================================================
// 引用计数
// ============================================================================

// CircularBuffer 可经 CircularBufferPtr（utils::SharedPtr）持有，
// 引用计数归零时析构，底层 2×size 内存被释放（munmap）
TEST(CircularBufferTest, SharedPtrHoldsAndReleases)
{
    auto* const raw = new CircularBuffer(256);
    {
        CircularBufferPtr ptr(raw);
        EXPECT_EQ(ptr.Get(), raw);
        EXPECT_EQ(raw->GetRefCount(), 1);
        EXPECT_EQ(ptr->size(), 256u);
    }
}

// ============================================================================
// 越界分配
// ============================================================================

#ifndef NDEBUG
// 越界分配：debug 构建下 LOG_ASSERT 生效，触发 abort。
// release（NDEBUG）下 LOG_ASSERT 为空操作，超限分配会静默越界，
// 最终由物理末尾的 PROT_NONE guard page 兜底触发 SIGSEGV（设计可接受，不在本测试覆盖）。
TEST(CircularBufferTest, AllocateOverflowDeath)
{
    constexpr size_t kSize = 256;
    CircularBuffer buffer(kSize);

    buffer.allocate(kSize - 1);
    EXPECT_DEATH(buffer.allocate(2), ".*");
}
#endif
