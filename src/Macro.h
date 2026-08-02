#pragma once

// 平台宏定义
#if defined(_WIN32) || defined(_WIN64)
#define PLATFORM_WINDOWS 1
#elif defined(__ANDROID__)
#define PLATFORM_ANDROID 1
#elif defined(__APPLE__)
#define PLATFORM_APPLE 1
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#define PLATFORM_IOS 1
#elif TARGET_OS_OSX
#define PLATFORM_MACOS 1
#endif
#endif

#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_ANDROID) || defined(PLATFORM_APPLE)
#define BACKEND_SUPPORT_VULKAN 1
#endif
