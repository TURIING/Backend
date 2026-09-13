#include "JobSystem.h"

#if PLATFORM_APPLE
#include <pthread.h>
#endif

BEGIN_NS_BACKEND

void JobSystem::SetThreadName(char const *name) noexcept {
#if PLATFORM_APPLE
    // macOS 的 pthread_setname_np 只作用于当前线程，且名字超长会被截断
    pthread_setname_np(name);
#else
    (void)name;
#endif
}

void JobSystem::SetThreadPriority(Priority priority) noexcept { (void)priority; }

END_NS_BACKEND
