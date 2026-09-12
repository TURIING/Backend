#pragma once
#include "Utils/Macro.h"
#include "Utils/Utils.h"

#include <array>
#include <cstddef>

// 启用 Vulkan beta 扩展（VK_KHR_portability_subset 等，MoltenVK 需要）
#define VK_ENABLE_BETA_EXTENSIONS
#include <cstdint>

#include "volk.h"

#define BEGIN_NS_BACKEND namespace Backend {
#define END_NS_BACKEND   }
#define NS_BD            Backend

BEGIN_NS_BACKEND

static constexpr size_t MAX_VERTEX_ATTRIBUTE_COUNT = 16;

enum class BackendType { AUTO, OPENGL, VULKAN };

/**
 * 立体渲染技术
 */
enum class StereoscopicType : uint8_t {
    NONE,       //!< 不启用立体渲染
    INSTANCED,  //!< 使用 instanced 渲染
    MULTIVIEW,  //!< 使用图形后端的多视图特性
};

/**
 * GPU 上下文优先级（控制 GPU 工作调度与抢占）
 */
enum class GpuContextPriority : uint8_t {
    DEFAULT,   //!< 后端默认（通常为 MEDIUM）
    LOW,       //!< 非交互、可延迟负载
    MEDIUM,    //!< 标准应用的默认级别
    HIGH,      //!< 高优先级、延迟敏感负载
    REALTIME,  //!< 系统关键实时应用（如 VR/AR compositor）
};

/**
 * 缓冲区使用方式；STATIC/DYNAMIC 为 legacy 位置值，DYNAMIC_BIT/SHARED_WRITE_BIT 为位标志（历史兼容混用）
 */
enum class BufferUsage : uint8_t {
    STATIC           = 0,     //!< 内容修改一次、使用多次
    DYNAMIC          = 1,     //!< 内容频繁修改、使用多次
    DYNAMIC_BIT      = 0x1,   //!< 可频繁修改（位标志）
    SHARED_WRITE_BIT = 0x04,  //!< 可内存映射写（位标志）
};

enum class ElementType : uint8_t {
    BYTE,
    BYTE2,
    BYTE3,
    BYTE4,
    UBYTE,
    UBYTE2,
    UBYTE3,
    UBYTE4,
    SHORT,
    SHORT2,
    SHORT3,
    SHORT4,
    USHORT,
    USHORT2,
    USHORT3,
    USHORT4,
    INT,
    UINT,
    FLOAT,
    FLOAT2,
    FLOAT3,
    FLOAT4,
    HALF,
    HALF2,
    HALF3,
    HALF4,
};

struct DriverConfig {
    StereoscopicType   stereoscopicType   = StereoscopicType::NONE;
    GpuContextPriority gpuContextPriority = GpuContextPriority::DEFAULT;

    bool        vulkanEnableAsyncPipelineCachePrewarming = false;
    bool        disableParallelShaderCompile             = false;
    bool        vulkanEnableStagingBufferBypass          = false;
    std::size_t handleArenaSize                          = 0;
};

struct Attribute {
    static constexpr uint8_t FLAG_NONE       = 0x00;
    static constexpr uint8_t FLAG_NORMALIZED = BIT(0);
    static constexpr uint8_t FLAG_INTEGER    = BIT(1);
    static constexpr uint8_t BUFFER_UNUSED   = 0xff;

    uint32_t    offset = 0;
    uint8_t     stride = 0;
    uint8_t     buffer = BUFFER_UNUSED;
    ElementType type   = ElementType::BYTE;
    uint8_t     flags  = FLAG_NONE;
};
using AttributeArray = std::array<Attribute, MAX_VERTEX_ATTRIBUTE_COUNT>;

END_NS_BACKEND
