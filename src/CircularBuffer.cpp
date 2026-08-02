/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "CircularBuffer.h"

#include <cstdlib>

#if !defined(WIN32) && !defined(__EMSCRIPTEN__)
#    include <sys/mman.h>
#    include <unistd.h>
#    define HAS_MMAP 1
#else
#    define HAS_MMAP 0
#endif

#if defined(WIN32)
#    include <windows.h>
#endif

BEGIN_NS_BACKEND

// 获取系统页大小
static size_t getPageSize() noexcept {
#if HAS_MMAP
    long const page = sysconf(_SC_PAGESIZE);
    return static_cast<size_t>(page > 0 ? page : 4096);
#elif defined(WIN32)
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwPageSize;
#else
    return 4096;
#endif
}

size_t CircularBuffer::sPageSize = getPageSize();

CircularBuffer::CircularBuffer(size_t size)
        : m_size(size) {
    m_data = alloc(size);
    m_tail = m_data;
    m_head = m_data;
}

CircularBuffer::~CircularBuffer() noexcept {
    dealloc();
}

void* CircularBuffer::alloc(size_t size) {
#if HAS_MMAP
    size_t const blockSize = getBlockSize();

    // 申请 2×size 的连续虚拟地址空间：前半是逻辑区域，后半承接回绕写入。
    void* data = ::mmap(nullptr, size * 2 + blockSize, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (data == MAP_FAILED) {
        LOG_CRITICAL("couldn't allocate {} KiB of virtual address space for the command buffer",
                (size * 2 / 1024));
    }

    // 末尾 guard page：防止命令流越过物理末尾写坏内存
    ::mprotect(static_cast<char*>(data) + size * 2, blockSize, PROT_NONE);
    return data;
#elif defined(WIN32)
    size_t const blockSize = getBlockSize();

    // Windows 下用 VirtualAlloc 预留虚拟地址空间，便于通过 VirtualProtect 设置页保护
    void* data = VirtualAlloc(nullptr, size * 2 + blockSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (data == nullptr) {
        LOG_CRITICAL("couldn't allocate {} KiB of virtual address space for the command buffer",
                (size * 2 / 1024));
    }

    // guard page：防止命令流越过物理末尾写坏内存
    void* guard = static_cast<char*>(data) + size * 2;
    DWORD oldProtect = 0;
    BOOL const ok = VirtualProtect(guard, blockSize, PAGE_NOACCESS, &oldProtect);
    if (!ok) {
        LOG_CRITICAL("VirtualProtect failed to set guard page");
    }
    return data;
#else
    return std::malloc(size * 2);
#endif
}

void CircularBuffer::dealloc() noexcept {
#if HAS_MMAP
    if (m_data) {
        ::munmap(m_data, m_size * 2 + getBlockSize());
    }
#elif defined(WIN32)
    if (m_data) {
        VirtualFree(m_data, 0, MEM_RELEASE);
    }
#else
    std::free(m_data);
#endif
    m_data = nullptr;
}

CircularBuffer::Range CircularBuffer::getBuffer() noexcept {
    Range const range{ m_tail, m_head };

    char* const pData = static_cast<char*>(m_data);
    char const* const pEnd = pData + m_size;
    char const* const pHead = static_cast<char const*>(m_head);
    if (pHead >= pEnd) {
        // 软环形：数据已线性落在 2×size 物理区域内，回绕后下一轮从起始处重新写入
        m_head = m_data;
    }
    m_tail = m_head;
    return range;
}

END_NS_BACKEND
