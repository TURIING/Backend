#pragma once

#include "Backend/Driver.h"

#include <cstddef>
#include <cstdint>

BEGIN_NS_BACKEND

// 命令基类：持有执行函数指针；通过输出参数返回下一条命令的偏移量，
// 使执行循环可以被编译器尾调用优化
class CommandBase {
protected:
    using Executor = void (*)(Driver& driver, CommandBase* self, intptr_t* next);

    constexpr explicit CommandBase(Executor const execute) noexcept : m_execute(execute) {}

public:
    static constexpr size_t kObjectAlignment = alignof(std::max_align_t);

    static constexpr size_t Align(size_t const v) { return (v + (kObjectAlignment - 1)) & -kObjectAlignment; }

    CommandBase* Execute(Driver& driver) {
        intptr_t next;
        m_execute(driver, this, &next);
        return reinterpret_cast<CommandBase*>(reinterpret_cast<intptr_t>(this) + next);
    }

    ~CommandBase() noexcept = default;

private:
    Executor m_execute;
};

// 跳过命令：标记命令块末尾，直接跳到下一条命令；next 为 nullptr 时结束命令链
class alignas(CommandBase::kObjectAlignment) NoopCommand : public CommandBase {
    intptr_t    m_next;
    static void Execute(Driver&, CommandBase* self, intptr_t* next) noexcept {
        *next = static_cast<NoopCommand*>(self)->m_next;
    }

public:
    // reinterpret_cast 无法在常量表达式中求值，构造函数不得为 constexpr
    explicit NoopCommand(void* next) noexcept
        : CommandBase(Execute),
          m_next(static_cast<intptr_t>(reinterpret_cast<char*>(next) - reinterpret_cast<char*>(this))) {}
};

END_NS_BACKEND
