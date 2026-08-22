#include "CommandStream.h"

#include <utility>

BEGIN_NS_BACKEND

CommandStream::CommandStream(DriverPtr driver, utils::CircularBuffer& buffer) noexcept
        : m_driver(std::move(driver)),
          m_currentBuffer(buffer),
          m_dispatcher(m_driver->GetDispatcher())
#ifndef NDEBUG
          , m_threadId(std::this_thread::get_id())
#endif
{
}

void CommandStream::Execute(void* buffer) {
    Driver& driver = *m_driver;
    CommandBase* base = static_cast<CommandBase*>(buffer);
    m_driver->Execute([&driver, base] {
        auto p = base;
        while (p) {
            p = p->Execute(driver);
        }
    });
}

void CommandStream::QueueCommand(std::function<void()> command) {
    new(AllocateCommand(sizeof(CustomCommand))) CustomCommand(std::move(command));
}

void CustomCommand::Execute(Driver&, CommandBase* base, intptr_t* next) {
    *next = sizeof(CustomCommand);
    static_cast<CustomCommand*>(base)->m_command();
    static_cast<CustomCommand*>(base)->~CustomCommand();
}

END_NS_BACKEND
