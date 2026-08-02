#pragma once
#include "Utils/Utils.h"

// 启用 Vulkan beta 扩展（VK_KHR_portability_subset 等，MoltenVK 需要）
#define VK_ENABLE_BETA_EXTENSIONS
#include "volk.h"

#include <cstdint>

#define BEGIN_NS_BACKEND namespace Backend {
#define END_NS_BACKEND }
#define NS_BD Backend
#define NS_UTILS utils

BEGIN_NS_BACKEND

enum class BackendType { AUTO, OPENGL, VULKAN };

/**
 * 立体渲染技术
 */
enum class StereoscopicType : uint8_t {
    NONE,      //!< 不启用立体渲染
    INSTANCED, //!< 使用 instanced 渲染
    MULTIVIEW, //!< 使用图形后端的多视图特性
};

/**
 * GPU 上下文优先级（控制 GPU 工作调度与抢占）
 */
enum class GpuContextPriority : uint8_t {
    DEFAULT,  //!< 后端默认（通常为 MEDIUM）
    LOW,      //!< 非交互、可延迟负载
    MEDIUM,   //!< 标准应用的默认级别
    HIGH,     //!< 高优先级、延迟敏感负载
    REALTIME, //!< 系统关键实时应用（如 VR/AR compositor）
};

struct DriverConfig {
    StereoscopicType stereoscopicType = StereoscopicType::NONE;
    GpuContextPriority gpuContextPriority = GpuContextPriority::DEFAULT;

    bool vulkanEnableAsyncPipelineCachePrewarming = false;
    bool disableParallelShaderCompile   = false;
    bool vulkanEnableStagingBufferBypass = false;
};

END_NS_BACKEND
