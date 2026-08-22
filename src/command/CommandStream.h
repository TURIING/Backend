#pragma once

#include "Backend/Driver.h"
#include "Command.h"
#include "Dispatcher.h"

#include "Utils/buffer/CircularBuffer.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

#ifndef NDEBUG
#include <thread>
#endif

// 按 2 的幂对齐（a 必须为 2 的幂）
#define ALIGN_UP(v, a) (((v) + ((a) - 1)) & ~((a) - 1))

BEGIN_NS_BACKEND

// 以成员指针直调驱动方法，记录端不经过虚函数表
template <typename M, typename D, typename T>
constexpr decltype(auto) Apply(M&& m, D&& d, T&& t) {
    return std::apply(
        [&](auto&&... args) { return (std::forward<D>(d).*std::forward<M>(m))(std::forward<decltype(args)>(args)...); },
        std::forward<T>(t));
}

// 命令类型绑定具体 Driver 方法，仅以 tuple 保存参数，执行时解包直调
template <typename... ARGS>
struct CommandType;

template <typename... ARGS>
struct CommandType<void (Driver::*)(ARGS...)> {
    template <void (Driver::*)(ARGS...)>
    class alignas(CommandBase::kObjectAlignment) Command : public CommandBase {
        using SavedParameters = std::tuple<std::remove_reference_t<ARGS>...>;
        SavedParameters m_args;

    public:
        template <typename M, typename D>
        static void Execute(M&& method, D&& driver, CommandBase* base, intptr_t* next) {
            static_assert(alignof(Command) <= CommandBase::kObjectAlignment);
            static_assert((sizeof(Command) % CommandBase::kObjectAlignment) == 0);
            auto* self = static_cast<Command*>(base);
            *next      = sizeof(Command);
            Apply(std::forward<M>(method), std::forward<D>(driver), std::move(self->m_args));
            self->~Command();
        }

        Command(Command&& rhs) noexcept = default;

        template <typename... A>
        explicit constexpr Command(Executor const execute, A&&... args) noexcept
            : CommandBase(execute), m_args(std::forward<A>(args)...) {}

        // 重载 placement new，避免编译器对命令对象构造做空指针检查
        void* operator new(std::size_t, void* ptr) noexcept {
            LOG_ASSERT(ptr);
            return ptr;
        }
    };
};

#define COMMAND_TYPE(method) CommandType<decltype(&Driver::method)>::Command<&Driver::method>

class alignas(CommandBase::kObjectAlignment) CustomCommand : public CommandBase {
    std::function<void()> m_command;
    static void           Execute(Driver&, CommandBase* base, intptr_t* next);

public:
    CustomCommand(CustomCommand&& rhs) noexcept = default;

    explicit CustomCommand(std::function<void()> cmd) : CommandBase(Execute), m_command(std::move(cmd)) {}
};

// 命令流：记录线程把 Driver 调用序列化写入环形缓冲，执行线程 Execute() 逐条回放
class CommandStream : public NS_UTILS::Ref {
public:
    CommandStream(DriverPtr driver, utils::CircularBuffer& buffer) noexcept;

    CommandStream(CommandStream const&) noexcept            = delete;
    CommandStream& operator=(CommandStream const&) noexcept = delete;

    utils::CircularBuffer const& GetCircularBuffer() const noexcept { return m_currentBuffer; }

#undef DECL_DRIVER_API
#define DECL_DRIVER_API(methodName, paramsDecl, params)                    \
    inline void methodName(paramsDecl) noexcept {                          \
        using Cmd     = COMMAND_TYPE(methodName);                          \
        void* const p = AllocateCommand(sizeof(Cmd));                      \
        new (p) Cmd(m_dispatcher.methodName##_, APPLY(std::move, params)); \
    }

#undef DECL_DRIVER_API_SYNCHRONOUS
#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params)         \
    inline RetType methodName(paramsDecl) noexcept {                                 \
        return Apply(&Driver::methodName, *m_driver, std::forward_as_tuple(params)); \
    }

#undef DECL_DRIVER_API_RETURN
#define DECL_DRIVER_API_RETURN(RetType, methodName, paramsDecl, params)                     \
    inline RetType methodName(paramsDecl) noexcept {                                        \
        RetType result = m_driver->methodName##S();                                         \
        using Cmd      = COMMAND_TYPE(methodName##R);                                       \
        void* const p  = AllocateCommand(sizeof(Cmd));                                      \
        new (p) Cmd(m_dispatcher.methodName##_, RetType(result), APPLY(std::move, params)); \
        return result;                                                                      \
    }

#include "Backend/DriverAPI.inc"

    // debug 构建下校准记录线程，渲染循环首帧调用
    void DebugThreading() noexcept {
#ifndef NDEBUG
        m_threadId = std::this_thread::get_id();
#endif
    }

    void Execute(void* buffer);

    // 以 lambda 记录命令，比 Driver 方法派发低效
    void QueueCommand(std::function<void()> command);

    // 分配与命令块同生命周期的辅助内存，执行后自动释放（不调用析构）
    inline void* Allocate(size_t size, size_t alignment = 8) noexcept;
    template <typename PodType, typename = std::enable_if_t<std::is_trivially_destructible_v<PodType>>>
    PodType* AllocatePod(size_t count = 1, size_t alignment = alignof(PodType)) noexcept;

private:
    void* AllocateCommand(size_t const size) noexcept {
#ifndef NDEBUG
        LOG_ASSERT(std::this_thread::get_id() == m_threadId);
#endif
        return m_currentBuffer.Allocate(size);
    }

    DriverPtr              m_driver;
    utils::CircularBuffer& m_currentBuffer;
    Dispatcher             m_dispatcher;

#ifndef NDEBUG
    std::thread::id m_threadId{};
#endif
};

DECLARE_SHARE_PTR_CLASS(CommandStream);

void* CommandStream::Allocate(size_t const size, size_t const alignment) noexcept {
    LOG_ASSERT(alignment && !(alignment & alignment - 1));

    // 预留 NoopCommand 与对齐填充，辅助内存由 NoopCommand 跳过
    const size_t s = CustomCommand::Align(sizeof(NoopCommand) + size + alignment - 1);

    char* const p = static_cast<char*>(AllocateCommand(s));
    new (p) NoopCommand(p + s);

    void* data = reinterpret_cast<void*>(ALIGN_UP(uintptr_t(p) + sizeof(NoopCommand), alignment));
    LOG_ASSERT(data >= p + sizeof(NoopCommand));
    return data;
}

template <typename PodType, typename>
PodType* CommandStream::AllocatePod(size_t const count, size_t const alignment) noexcept {
    return static_cast<PodType*>(Allocate(count * sizeof(PodType), alignment));
}

END_NS_BACKEND
