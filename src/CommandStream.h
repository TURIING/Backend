#pragma once

#include "Backend/Driver.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>

#include "CircularBuffer.h"

#ifndef NDEBUG
#include <thread>
#endif

BEGIN_NS_BACKEND

/**
 * @brief 命令基类：持有执行函数指针，命令链的入口
 *
 * 每个命令对象写入命令流后，通过 execute() 调用函数指针执行并返回下一条命令的地址。
 * 利用返回下一条命令偏移量的方式，使执行循环可以被编译器尾调用优化。
 */
class CommandBase {
protected:
    // 命令执行函数签名；driver 为将来的 Driver 方法派发保留，当前命令类型未使用
    using Execute = void (*)(Driver& driver, CommandBase* self, intptr_t* next);

    constexpr explicit CommandBase(Execute const execute) noexcept : m_execute(execute) {}

public:
    // 命令流中所有命令的对齐粒度
    static constexpr size_t kObjectAlignment = alignof(std::max_align_t);

    /** @brief 将 v 向上对齐到对齐粒度 */
    static constexpr size_t align(size_t const v) { return (v + (kObjectAlignment - 1)) & -kObjectAlignment; }

    /**
     * @brief 执行当前命令并返回下一条命令
     * @param driver 驱动实例
     * @return 下一条命令，nullptr 表示命令链结束
     */
    CommandBase* execute(Driver& driver) {
        // 通过输出参数返回下一条命令的偏移量，便于编译器对 m_execute 调用做尾调用优化
        intptr_t next;
        m_execute(driver, this, &next);
        return reinterpret_cast<CommandBase*>(reinterpret_cast<intptr_t>(this) + next);
    }

    ~CommandBase() noexcept = default;

private:
    Execute m_execute;
};

/**
 * @brief 包装 std::function 的命令，供 queueCommand() 记录 lambda
 */
class alignas(CommandBase::kObjectAlignment) CustomCommand : public CommandBase {
    std::function<void()> m_command;
    // 执行 lambda 并推进命令链
    static void execute(Driver& driver, CommandBase* base, intptr_t* next);

public:
    CustomCommand(CustomCommand&& rhs) noexcept = default;

    /** @brief 以 lambda 构造命令 */
    explicit CustomCommand(std::function<void()> cmd) : CommandBase(execute), m_command(std::move(cmd)) {}
};

/**
 * @brief 跳过命令：用于 allocate() 插入的辅助内存块，直接跳到下一条命令
 */
class alignas(CommandBase::kObjectAlignment) NoopCommand : public CommandBase {
    intptr_t    m_next;
    static void execute(Driver&, CommandBase* self, intptr_t* next) noexcept {
        *next = static_cast<NoopCommand*>(self)->m_next;
    }

public:
    /** @brief 构造跳过命令，next 为下一条命令地址（nullptr 表示命令链终止） */
    constexpr explicit NoopCommand(void* next) noexcept
        : CommandBase(execute), m_next(intptr_t((char*)next - (char*)this)) {}
};

/**
 * @brief 命令流：记录线程向 CircularBuffer 写入命令
 *
 * 提供三类写入：
 *  - queueCommand()：记录一条 lambda 命令（比 Driver 方法派发低效）
 *  - allocate()/allocatePod()：分配辅助内存，随命令块执行后自动释放（不调用析构）
 *  - Driver 方法派发：当前 Driver 尚为占位实现，待其 API 成型后再补充
 *    CommandType<>/COMMAND_TYPE 派发机制
 *
 * 继承 Ref 以对齐项目对象模型；持有 DriverPtr 引用计数保活 Driver。
 */
class CommandStream : public NS_UTILS::Ref {
public:
    /**
     * @brief 构造命令流
     * @param driver 驱动实例（SharedPtr 保活；当前派发未使用，为将来的 Driver 方法派发保留）
     * @param buffer 目标环形缓冲区
     */
    CommandStream(DriverPtr driver, CircularBuffer& buffer) noexcept;

    CommandStream(CommandStream const& rhs) noexcept            = delete;
    CommandStream& operator=(CommandStream const& rhs) noexcept = delete;

    /** @brief 获取当前环形缓冲区 */
    CircularBuffer const& getCircularBuffer() const noexcept { return m_currentBuffer; }

    /**
     * @brief 记录线程进入记录循环前调用，debug 构建下断言单线程写入
     */
    void debugThreading() noexcept {
#ifndef NDEBUG
        m_threadId = std::this_thread::get_id();
#endif
    }

    /**
     * @brief 执行命令块
     * @param buffer 由 CommandBufferQueue::waitForCommands() 返回的命令块首地址
     */
    void execute(void* buffer);

    /**
     * @brief 记录一条 lambda 命令
     * @param command 待执行的 lambda
     */
    void queueCommand(std::function<void()> command);

    /**
     * @brief 在命令块中分配辅助内存，命令块执行后自动释放
     * @param size      所需字节数
     * @param alignment 对齐要求（必须为 2 的幂）
     * @note 不会调用析构函数
     */
    inline void* allocate(size_t size, size_t alignment = 8) noexcept;

    /**
     * @brief 分配平凡析构对象的数组
     * @tparam PodType 平凡析构类型
     */
    template <typename PodType, typename = std::enable_if_t<std::is_trivially_destructible_v<PodType>>>
    PodType* allocatePod(size_t count = 1, size_t alignment = alignof(PodType)) noexcept;

private:
    void* allocateCommand(size_t const size) noexcept {
#ifndef NDEBUG
        LOG_ASSERT(std::this_thread::get_id() == m_threadId);
#endif
        return m_currentBuffer.allocate(size);
    }

    // SharedPtr 保活 Driver，消费线程执行命令期间 Driver 不会被释放
    DriverPtr       m_driver;
    CircularBuffer& m_currentBuffer;

#ifndef NDEBUG
    // 仅用于调试：断言命令流只被单一线程写入
    std::thread::id m_threadId{};
#endif
};

DECLARE_SHARE_PTR_CLASS(CommandStream);

void* CommandStream::allocate(size_t const size, size_t const alignment) noexcept {
    // 对齐必须是 2 的幂
    LOG_ASSERT(alignment && !(alignment & alignment - 1));

    // 补齐大小以容纳 NoopCommand 与对齐余量
    size_t const s = CommandBase::align(sizeof(NoopCommand) + size + alignment - 1);

    // 在命令流中分配空间并插入 NoopCommand 跳过整块辅助内存
    char* const p = static_cast<char*>(allocateCommand(s));
    new (p) NoopCommand(p + s);

    // 计算"用户"数据指针（对齐到 alignment）
    void* data = reinterpret_cast<void*>((uintptr_t(p) + sizeof(NoopCommand) + alignment - 1) & ~(alignment - 1));
    LOG_ASSERT(data >= static_cast<void*>(p + sizeof(NoopCommand)));
    return data;
}

template <typename PodType, typename>
PodType* CommandStream::allocatePod(size_t const count, size_t const alignment) noexcept {
    return static_cast<PodType*>(allocate(count * sizeof(PodType), alignment));
}

END_NS_BACKEND
