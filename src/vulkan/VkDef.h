#pragma once

#include <utility>
#include <vector>

#include "Backend/DriverDefine.h"
#include "Utils/Macro.h"
// 格式清单与 Ycbcr 相关定义已收敛到 Definitions.h，此处透出以保持既有包含链不变
#include "vulkan/utils/Definitions.h"
#include "vulkan/utils/Image.h"

#ifdef BACKEND_SUPPORT_VULKAN
BEGIN_NS_BACKEND
constexpr uint32_t INVALID_VK_INDEX            = UINT32_MAX;
constexpr uint32_t kRequiredVulkanVersionMajor = 1;
constexpr uint32_t kRequiredVulkanVersionMinor = 1;

constexpr uint32_t kUndefinedVkExtent = 0xFFFFFFFF;  // surface capabilities 用该值表示 currentExtent 由调用方决定

// vkCreate*/vkDestroy* 的分配器实参统一用默认分配器，此常量用于标注该实参的含义
constexpr VkAllocationCallbacks const *kVkAlloc = nullptr;

// VulkanCommands 同时管理的命令缓冲上限；经验值：三缓冲 × 单帧最大 renderpass 数
constexpr int kMaxCommandBuffers = 3 * 15;

// 既有代码在 Backend 作用域直接使用这三个符号，这里保留同名别名指向 VK_UTILS 中的定义
using VkFormatList   = VK_UTILS::VkFormatList;
using VK_UTILS::ALL_VK_FORMATS;
using VK_UTILS::EXT_VK_FORMATS;
// 上游把 VulkanLayout 置于后端命名空间，纹理与渲染通道的签名沿用其裸写法
using VK_UTILS::VulkanLayout;

/********************************** Debug flag **************************************/

#define BVK_DEBUG_SYSTRACE 0x00000001
#define BVK_DEBUG_GROUP_MARKERS 0x00000002
#define BVK_DEBUG_TEXTURE 0x00000004
#define BVK_DEBUG_LAYOUT_TRANSITION 0x00000008
#define BVK_DEBUG_COMMAND_BUFFER 0x00000010
#define BVK_DEBUG_DUMP_API 0x00000020
#define BVK_DEBUG_VALIDATION 0x00000040
#define BVK_DEBUG_PRINT_GROUP_MARKERS 0x00000080
#define BVK_DEBUG_BLIT_FORMAT 0x00000100
#define BVK_DEBUG_BLITTER 0x00000200
#define BVK_DEBUG_FBO_CACHE 0x00000400
#define BVK_DEBUG_SHADER_MODULE 0x00000800
#define BVK_DEBUG_READ_PIXELS 0x00001000
#define BVK_DEBUG_PIPELINE_CACHE 0x00002000
#define BVK_DEBUG_STAGING_ALLOCATION 0x00004000
#define BVK_DEBUG_DEBUG_UTILS 0x00008000
#define BVK_DEBUG_RESOURCE_LEAK 0x00010000
#define BVK_DEBUG_FORCE_LOG_TO_I 0x00020000
#define BVK_DEBUG_PROFILING 0x00040000
#define BVK_DEBUG_VULKAN_BUFFER_CACHE 0x00080000
#define BVK_DEBUG_EVERYTHING (0xFFFFFFFF & ~BVK_DEBUG_PROFILING)
#define BVK_DEBUG_PERFORMANCE BVK_DEBUG_SYSTRACE

#if defined(BACKEND_DEBUG_FLAG)
#define BVK_DEBUG_FORWARDED_FLAG (BACKEND_DEBUG_FLAG & BVK_DEBUG_EVERYTHING)
#else
#define BVK_DEBUG_FORWARDED_FLAG 0
#endif

// debug 构建 = 性能 trace + 外部注入标志；release 构建 = 0
#ifndef NDEBUG
#define BVK_DEBUG_FLAGS (BVK_DEBUG_PERFORMANCE | BVK_DEBUG_FORWARDED_FLAG)
#else
#define BVK_DEBUG_FLAGS 0
#endif

#define BVK_ENABLED(flags) (((BVK_DEBUG_FLAGS) & (flags)) == (flags))

END_NS_BACKEND
#endif