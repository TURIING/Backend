/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
