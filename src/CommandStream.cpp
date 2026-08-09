#include "CommandStream.h"

#include <functional>
#include <utility>

BEGIN_NS_BACKEND

CommandStream::CommandStream(DriverPtr driver, CircularBuffer& buffer) noexcept
        : m_driver(std::move(driver)),
          m_currentBuffer(buffer)
#ifndef NDEBUG
          , m_threadId(std::this_thread::get_id())
#endif
{
}

void CommandStream::execute(void* buffer) {
    Driver& driver = *m_driver;
    CommandBase* base = static_cast<CommandBase*>(buffer);
    while (base) {
        base = base->execute(driver);
    }
}

void CommandStream::queueCommand(std::function<void()> command) {
    new(allocateCommand(sizeof(CustomCommand))) CustomCommand(std::move(command));
}

void CustomCommand::execute(Driver&, CommandBase* base, intptr_t* next) {
    *next = sizeof(CustomCommand);
    static_cast<CustomCommand*>(base)->m_command();
    static_cast<CustomCommand*>(base)->~CustomCommand();
}

END_NS_BACKEND
