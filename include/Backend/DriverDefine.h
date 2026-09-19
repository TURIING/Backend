#pragma once
#include "Backend/Namespace.h"
#include "Backend/platform/Platform.h"

#include "Utils/Debug.h"
#include "Utils/Macro.h"
#include "Utils/Utils.h"
#include "Utils/math/Vector.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <variant>
#include <vector>

// 启用 Vulkan beta 扩展（VK_KHR_portability_subset 等，MoltenVK 需要）
#define VK_ENABLE_BETA_EXTENSIONS
#include "volk.h"

#include "Backend/Handle.h"

BEGIN_NS_BACKEND

// 权威定义在 Platform.h（Platform::DriverConfig 需要它们），此处只做便捷别名
using StereoscopicType   = Platform::StereoscopicType;
using GpuContextPriority = Platform::GpuContextPriority;
using AsynchronousMode   = Platform::AsynchronousMode;
using DriverConfig       = Platform::DriverConfig;

static constexpr size_t MAX_VERTEX_ATTRIBUTE_COUNT = 16;
static constexpr size_t MAX_VERTEX_BUFFER_COUNT    = 16;

static constexpr size_t MAX_SAMPLER_COUNT            = 62;  //!< 特性级别 3 所需上限
static constexpr size_t MAX_DESCRIPTOR_SET_COUNT     = 4;   //!< Vulkan 保证值
static constexpr size_t CONFIG_UNIFORM_BINDING_COUNT = 9;   //!< OpenGL ES 保证值
static constexpr size_t CONFIG_SAMPLER_BINDING_COUNT = 4;   //!< OpenGL ES 保证值

enum class BackendType { AUTO, OPENGL, VULKAN };

// 交换链创建标志；取值与上游 Filament 保持一致
constexpr uint64_t kSwapChainConfigSRGBColorspace   = 0x10;  //!< 交换链自动执行 linear→sRGB 编码
constexpr uint64_t kSwapChainConfigHasStencilBuffer = 0x20;  //!< 交换链需包含 stencil 分量
constexpr uint64_t kSwapChainConfigProtectedContent = 0x40;  //!< 交换链承载受保护内容

/**
 * 围栏等待结果；数值与上游 Filament 保持一致
 */
enum class FenceStatus : int8_t {
    Error              = -1,  //!< 发生错误或等待被取消，条件未满足
    ConditionSatisfied = 0,   //!< 围栏条件已满足
    TimeoutExpired     = 1,   //!< 等待超时，条件未满足
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

constexpr BufferUsage operator|(BufferUsage lhs, BufferUsage rhs) noexcept {
    return static_cast<BufferUsage>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

constexpr BufferUsage operator&(BufferUsage lhs, BufferUsage rhs) noexcept {
    return static_cast<BufferUsage>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
}

// 任一标志命中即真；STATIC 恰为 0，故不提供 operator bool
constexpr bool HasAnyFlag(BufferUsage value, BufferUsage flags) noexcept {
    return static_cast<uint8_t>(value & flags) != 0;
}

/**
 * 缓冲对象在渲染管线中的绑定点
 */
enum class BufferObjectBinding : uint8_t {
    Vertex,
    Uniform,
    ShaderStorage,
};

// 着色器编译任务的优先级；取值顺序即队列下标，不可重排
enum class CompilerPriorityQueue : uint8_t {
    Critical,  //!< 立即需要
    High,      //!< 很快需要
    Low,       //!< 最终需要
};

constexpr size_t COMPILER_PRIORITY_QUEUE_COUNT = 3;

enum class ElementType : uint8_t {    BYTE,
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

/**
 * 后端特性级别：取值即能力档位标识，用于索引各档位容量表
 */
enum class FeatureLevel : uint8_t {
    FEATURE_LEVEL_0 = 0,  //!< OpenGL ES 2.0
    FEATURE_LEVEL_1,      //!< OpenGL ES 3.0（默认）
    FEATURE_LEVEL_2,      //!< OpenGL ES 3.1 + 16 纹理单元 + cubemap 数组
    FEATURE_LEVEL_3,      //!< OpenGL ES 3.1 + 31 纹理单元 + cubemap 数组
};

/**
 * 各特性级别的采样器数量下限；以 FeatureLevel 的值作下标，第 0 档不使用
 */
struct FeatureLevelCaps {
    size_t maxVertexSamplerCount;
    size_t maxFragmentSamplerCount;
};

constexpr FeatureLevelCaps kFeatureLevelCaps[4] = {
    { 0, 0 },    //!< 占位，不使用
    { 16, 16 },  //!< OpenGL ES / Vulkan / Metal / WebGPU 均保证
    { 16, 16 },  //!< 同上
    { 31, 31 },  //!< 仅 Metal 保证
};

/**
 * 计时器查询的状态；数值与 FenceStatus 同构
 */
enum class TimerQueryResult : int8_t {
    Error     = -1,  //!< 出错，结果不会可用
    NotReady  = 0,   //!< 结果尚未就绪
    Available = 1,   //!< 结果可用
};

//! 着色器语言
enum class ShaderLanguage {
    UNSPECIFIED   = -1,
    ESSL1         = 0,
    ESSL3         = 1,
    SPIRV         = 2,
    MSL           = 3,
    METAL_LIBRARY = 4,
    WGSL          = 5,
};

enum class ShaderStage : uint8_t {
    VERTEX   = 0,
    FRAGMENT = 1,
    COMPUTE  = 2,
};

enum class ShaderStageFlags : uint8_t {
    NONE                   = 0,
    VERTEX                 = 0x1,
    FRAGMENT               = 0x2,
    COMPUTE                = 0x4,
    ALL_SHADER_STAGE_FLAGS = VERTEX | FRAGMENT | COMPUTE,
};

constexpr ShaderStageFlags operator|(ShaderStageFlags lhs, ShaderStageFlags rhs) noexcept {
    return static_cast<ShaderStageFlags>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

constexpr ShaderStageFlags operator&(ShaderStageFlags lhs, ShaderStageFlags rhs) noexcept {
    return static_cast<ShaderStageFlags>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
}

// 任一标志命中即真；NONE 恰为 0，故不提供 operator bool
constexpr bool HasAnyFlag(ShaderStageFlags value, ShaderStageFlags flags) noexcept {
    return static_cast<uint8_t>(value & flags) != 0;
}

//! 纹理采样的数据类型
enum class TextureType : uint8_t {
    FLOAT,
    INT,
    UINT,
    DEPTH,
    STENCIL,
    DEPTH_STENCIL,
};

//! 描述符类型
enum class DescriptorType : uint8_t {
    SAMPLER_2D_FLOAT,
    SAMPLER_2D_INT,
    SAMPLER_2D_UINT,
    SAMPLER_2D_DEPTH,

    SAMPLER_2D_ARRAY_FLOAT,
    SAMPLER_2D_ARRAY_INT,
    SAMPLER_2D_ARRAY_UINT,
    SAMPLER_2D_ARRAY_DEPTH,

    SAMPLER_CUBE_FLOAT,
    SAMPLER_CUBE_INT,
    SAMPLER_CUBE_UINT,
    SAMPLER_CUBE_DEPTH,

    SAMPLER_CUBE_ARRAY_FLOAT,
    SAMPLER_CUBE_ARRAY_INT,
    SAMPLER_CUBE_ARRAY_UINT,
    SAMPLER_CUBE_ARRAY_DEPTH,

    SAMPLER_3D_FLOAT,
    SAMPLER_3D_INT,
    SAMPLER_3D_UINT,

    SAMPLER_2D_MS_FLOAT,
    SAMPLER_2D_MS_INT,
    SAMPLER_2D_MS_UINT,

    SAMPLER_2D_MS_ARRAY_FLOAT,
    SAMPLER_2D_MS_ARRAY_INT,
    SAMPLER_2D_MS_ARRAY_UINT,

    SAMPLER_EXTERNAL,
    UNIFORM_BUFFER,
    SHADER_STORAGE_BUFFER,
    INPUT_ATTACHMENT,
};

//! 描述符标志
enum class DescriptorFlags : uint8_t {
    NONE = 0x00,

    DYNAMIC_OFFSET = 0x01,  // UNIFORM_BUFFER 使用动态偏移

    UNFILTERABLE = 0x02,    // 纹理/采样器不做过滤
};

constexpr DescriptorFlags operator|(DescriptorFlags lhs, DescriptorFlags rhs) noexcept {
    return static_cast<DescriptorFlags>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

constexpr DescriptorFlags operator&(DescriptorFlags lhs, DescriptorFlags rhs) noexcept {
    return static_cast<DescriptorFlags>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
}

// 任一标志命中即真；NONE 恰为 0，故不提供 operator bool
constexpr bool HasAnyFlag(DescriptorFlags value, DescriptorFlags flags) noexcept {
    return static_cast<uint8_t>(value & flags) != 0;
}

using descriptor_set_t     = uint8_t;
using descriptor_binding_t = uint8_t;

// 动态 uniform 缓冲的偏移量数组。
//
// 上游把它实现为命令流内存里的一个非拥有视图（构造时从命令流 arena 取指针）。本项目
// 的命令流尚无该分配接口，且该数组会被 DescriptorSet 移动留存至 Commit 才读取，故改为
// 按值拥有——避免命令流推进后指针悬垂。
class DescriptorSetOffsetArray {
public:
    using value_type      = uint32_t;
    using reference       = value_type &;
    using const_reference = value_type const &;
    using size_type       = uint32_t;
    using pointer         = value_type *;
    using const_pointer   = value_type const *;
    using iterator        = pointer;
    using const_iterator  = const_pointer;

    DescriptorSetOffsetArray() noexcept = default;

    explicit DescriptorSetOffsetArray(size_type size) : m_offsets(size, 0) {}

    DescriptorSetOffsetArray(std::initializer_list<value_type> list) : m_offsets(list) {}

    DescriptorSetOffsetArray(DescriptorSetOffsetArray const &)            = delete;
    DescriptorSetOffsetArray &operator=(DescriptorSetOffsetArray const &) = delete;

    DescriptorSetOffsetArray(DescriptorSetOffsetArray &&) noexcept            = default;
    DescriptorSetOffsetArray &operator=(DescriptorSetOffsetArray &&) noexcept = default;

    ~DescriptorSetOffsetArray() noexcept = default;

    NODISCARD bool Empty() const noexcept { return m_offsets.empty(); }
    NODISCARD size_type Size() const noexcept { return static_cast<size_type>(m_offsets.size()); }

    NODISCARD pointer Data() noexcept { return m_offsets.data(); }
    NODISCARD const_pointer Data() const noexcept { return m_offsets.data(); }

    NODISCARD reference operator[](size_type n) noexcept { return m_offsets[n]; }
    NODISCARD const_reference operator[](size_type n) const noexcept { return m_offsets[n]; }

    NODISCARD iterator begin() noexcept { return m_offsets.data(); }
    NODISCARD const_iterator begin() const noexcept { return m_offsets.data(); }
    NODISCARD iterator end() noexcept { return m_offsets.data() + m_offsets.size(); }
    NODISCARD const_iterator end() const noexcept { return m_offsets.data() + m_offsets.size(); }

private:
    std::vector<value_type> m_offsets;
};

//! 描述符集布局中的单个绑定
struct DescriptorSetLayoutDescriptor {
    static bool IsSampler(DescriptorType type) noexcept {
        return static_cast<int>(type) <= static_cast<int>(DescriptorType::SAMPLER_EXTERNAL);
    }

    static bool IsBuffer(DescriptorType type) noexcept {
        return type == DescriptorType::UNIFORM_BUFFER || type == DescriptorType::SHADER_STORAGE_BUFFER;
    }

    DescriptorType       type;
    ShaderStageFlags     stageFlags;
    descriptor_binding_t binding;
    DescriptorFlags      flags = DescriptorFlags::NONE;
    uint16_t             count = 0;

    friend bool operator==(DescriptorSetLayoutDescriptor const& lhs,
                           DescriptorSetLayoutDescriptor const& rhs) noexcept {
        return lhs.type == rhs.type && lhs.flags == rhs.flags && lhs.count == rhs.count &&
               lhs.stageFlags == rhs.stageFlags;
    }
};

/**
 * 渲染目标缓冲区位掩码
 */
enum class TargetBufferFlags : uint32_t {
    NONE   = 0x0u,  //!< 不选任何缓冲区
    COLOR0 = 0x00000001u,
    COLOR1 = 0x00000002u,
    COLOR2 = 0x00000004u,
    COLOR3 = 0x00000008u,
    COLOR4 = 0x00000010u,
    COLOR5 = 0x00000020u,
    COLOR6 = 0x00000040u,
    COLOR7 = 0x00000080u,

    COLOR     = COLOR0,       //!< 已废弃，等价 COLOR0
    COLOR_ALL = COLOR0 | COLOR1 | COLOR2 | COLOR3 | COLOR4 | COLOR5 | COLOR6 | COLOR7,
    DEPTH     = 0x10000000u,  //!< 深度缓冲区
    STENCIL   = 0x20000000u,  //!< 模板缓冲区

    DEPTH_AND_STENCIL = DEPTH | STENCIL,
    ALL               = COLOR_ALL | DEPTH | STENCIL,
};

constexpr TargetBufferFlags operator|(TargetBufferFlags lhs, TargetBufferFlags rhs) noexcept {
    return static_cast<TargetBufferFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

constexpr TargetBufferFlags operator&(TargetBufferFlags lhs, TargetBufferFlags rhs) noexcept {
    return static_cast<TargetBufferFlags>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

// 渲染通道里清除「除某个附件以外」的标志需要按位取反
constexpr TargetBufferFlags operator~(TargetBufferFlags value) noexcept {
    return static_cast<TargetBufferFlags>(~static_cast<uint32_t>(value));
}

// 任一标志命中即真；NONE 恰为 0，故不提供 operator bool
constexpr bool HasAnyFlag(TargetBufferFlags value, TargetBufferFlags flags) noexcept {
    return static_cast<uint32_t>(value & flags) != 0;
}

//! 缓冲区映射方式
enum class MapBufferAccessFlags : uint8_t {
    WRITE_BIT            = 0x2,  //!< 用于写入的映射
    INVALIDATE_RANGE_BIT = 0x4,  //!< 映射区间内容失效
};

constexpr MapBufferAccessFlags operator|(MapBufferAccessFlags lhs, MapBufferAccessFlags rhs) noexcept {
    return static_cast<MapBufferAccessFlags>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

constexpr MapBufferAccessFlags operator&(MapBufferAccessFlags lhs, MapBufferAccessFlags rhs) noexcept {
    return static_cast<MapBufferAccessFlags>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
}

// 任一标志命中即真；无零值成员，但保持与其余位标志枚举一致的判定入口
constexpr bool HasAnyFlag(MapBufferAccessFlags value, MapBufferAccessFlags flags) noexcept {
    return static_cast<uint8_t>(value & flags) != 0;
}

/**
 * 视口：裁剪空间的原点与范围，所有绘制都被裁剪到视口内
 */
struct Viewport {
    int32_t  left   = 0;
    int32_t  bottom = 0;
    uint32_t width  = 0;
    uint32_t height = 0;

    NODISCARD int32_t Right() const noexcept { return left + static_cast<int32_t>(width); }
    NODISCARD int32_t Top() const noexcept { return bottom + static_cast<int32_t>(height); }

    friend bool operator==(Viewport const& lhs, Viewport const& rhs) noexcept {
        return lhs.left == rhs.left && lhs.bottom == rhs.bottom && lhs.width == rhs.width &&
               lhs.height == rhs.height;
    }

    friend bool operator!=(Viewport const& lhs, Viewport const& rhs) noexcept { return !(lhs == rhs); }
};

/**
 * 近/远裁剪面到窗口坐标的映射
 */
struct DepthRange {
    float near = 0.0f;  //!< 近平面映射到窗口坐标的值
    float far  = 1.0f;  //!< 远平面映射到窗口坐标的值
};

/**
 * 着色器模型：区分移动端与桌面端功能/质量档位
 */
enum class ShaderModel : uint8_t {
    Mobile  = 1,
    Desktop = 2,
};

/**
 * 图元类型；取值与 GL 一致，不可改动
 */
enum class PrimitiveType : uint8_t {
    POINTS         = 0,
    LINES          = 1,
    LINE_STRIP     = 3,
    TRIANGLES      = 4,
    TRIANGLE_STRIP = 5,
};

//! 支持的 uniform 类型
enum class UniformType : uint8_t {
    BOOL,
    BOOL2,
    BOOL3,
    BOOL4,
    FLOAT,
    FLOAT2,
    FLOAT3,
    FLOAT4,
    INT,
    INT2,
    INT3,
    INT4,
    UINT,
    UINT2,
    UINT3,
    UINT4,
    MAT3,
    MAT4,
    STRUCT,
};

//! 支持的常量参数类型
enum class ConstantType : uint8_t {
    INT,
    FLOAT,
    BOOL,
};

enum class Precision : uint8_t {
    Low,
    Medium,
    High,
    Default,
};

//! 纹理采样器类型
enum class SamplerType : uint8_t {
    SAMPLER_2D,             //!< 2D 纹理
    SAMPLER_2D_ARRAY,       //!< 2D 数组纹理
    SAMPLER_CUBEMAP,        //!< cubemap 纹理
    SAMPLER_EXTERNAL,       //!< 外部纹理
    SAMPLER_3D,             //!< 3D 纹理
    SAMPLER_CUBEMAP_ARRAY,  //!< cubemap 数组纹理（特性级别 2）
};

//! 子通道类型
enum class SubpassType : uint8_t {
    SUBPASS_INPUT,
};

//! 采样器格式
enum class SamplerFormat : uint8_t {
    INT    = 0,  //!< 有符号整数采样器
    UINT   = 1,  //!< 无符号整数采样器
    FLOAT  = 2,  //!< 浮点采样器
    SHADOW = 3,  //!< 阴影采样器（PCF）
};

//! 面剔除模式
enum class CullingMode : uint8_t {
    None,          //!< 不剔除，正反面均可见
    Front,         //!< 剔除正面，仅背面可见
    Back,          //!< 剔除背面，仅正面可见
    FrontAndBack,  //!< 正反面均剔除，几何体不可见
};

//! 像素数据格式
enum class PixelDataFormat : uint8_t {
    R,                //!< 单红通道，float
    R_INTEGER,        //!< 单红通道，整数
    RG,               //!< 红绿两通道，float
    RG_INTEGER,       //!< 红绿两通道，整数
    RGB,              //!< 红绿蓝三通道，float
    RGB_INTEGER,      //!< 红绿蓝三通道，整数
    RGBA,             //!< 红绿蓝透明度四通道，float
    RGBA_INTEGER,     //!< 红绿蓝透明度四通道，整数
    UNUSED,           //!< 曾为 rgbm
    DEPTH_COMPONENT,  //!< 深度，通常 16 或 24 位
    DEPTH_STENCIL,    //!< 深度（24 位）+ 模板（8 位）
    ALPHA,            //!< 单透明度通道，float
};

//! 像素数据类型
enum class PixelDataType : uint8_t {
    UBYTE,                //!< 无符号字节
    BYTE,                 //!< 有符号字节
    USHORT,               //!< 无符号 16 位整数
    SHORT,                //!< 有符号 16 位整数
    UINT,                 //!< 无符号 32 位整数
    INT,                  //!< 有符号 32 位整数
    HALF,                 //!< 16 位浮点
    FLOAT,                //!< 32 位浮点
    COMPRESSED,           //!< 压缩像素，见 CompressedPixelDataType
    UINT_10F_11F_11F_REV, //!< 三个低精度浮点数
    USHORT_565,           //!< 无符号 16 位，编码 RGB 三通道
    UINT_2_10_10_10_REV,  //!< 无符号归一化 10 位 RGB + 2 位 alpha
};

//! 压缩像素数据类型
enum class CompressedPixelDataType : uint16_t {
    // GLES 3.0 与 GL 4.3 强制支持
    EAC_R11, EAC_R11_SIGNED, EAC_RG11, EAC_RG11_SIGNED,
    ETC2_RGB8, ETC2_SRGB8,
    ETC2_RGB8_A1, ETC2_SRGB8_A1,
    ETC2_EAC_RGBA8, ETC2_EAC_SRGBA8,

    // 除 Android/iOS 外均可用
    DXT1_RGB, DXT1_RGBA, DXT3_RGBA, DXT5_RGBA,
    DXT1_SRGB, DXT1_SRGBA, DXT3_SRGBA, DXT5_SRGBA,

    RGBA_ASTC_4x4,  // ASTC 需 GLES 扩展
    RGBA_ASTC_5x4,
    RGBA_ASTC_5x5,
    RGBA_ASTC_6x5,
    RGBA_ASTC_6x6,
    RGBA_ASTC_8x5,
    RGBA_ASTC_8x6,
    RGBA_ASTC_8x8,
    RGBA_ASTC_10x5,
    RGBA_ASTC_10x6,
    RGBA_ASTC_10x8,
    RGBA_ASTC_10x10,
    RGBA_ASTC_12x10,
    RGBA_ASTC_12x12,
    SRGB8_ALPHA8_ASTC_4x4,
    SRGB8_ALPHA8_ASTC_5x4,
    SRGB8_ALPHA8_ASTC_5x5,
    SRGB8_ALPHA8_ASTC_6x5,
    SRGB8_ALPHA8_ASTC_6x6,
    SRGB8_ALPHA8_ASTC_8x5,
    SRGB8_ALPHA8_ASTC_8x6,
    SRGB8_ALPHA8_ASTC_8x8,
    SRGB8_ALPHA8_ASTC_10x5,
    SRGB8_ALPHA8_ASTC_10x6,
    SRGB8_ALPHA8_ASTC_10x8,
    SRGB8_ALPHA8_ASTC_10x10,
    SRGB8_ALPHA8_ASTC_12x10,
    SRGB8_ALPHA8_ASTC_12x12,

    // RGTC 需 GLES 扩展
    RED_RGTC1,               //!< BC4 无符号
    SIGNED_RED_RGTC1,        //!< BC4 有符号
    RED_GREEN_RGTC2,         //!< BC5 无符号
    SIGNED_RED_GREEN_RGTC2,  //!< BC5 有符号

    // BPTC 需 GLES 扩展
    RGB_BPTC_SIGNED_FLOAT,    //!< BC6H 有符号
    RGB_BPTC_UNSIGNED_FLOAT,  //!< BC6H 无符号
    RGBA_BPTC_UNORM,          //!< BC7
    SRGB_ALPHA_BPTC_UNORM,    //!< BC7 sRGB
};

/**
 * 纹理存储格式。
 *
 * 枚举值名保留上游拼写：它们与 GL/Vulkan 格式名一一对应，是外部世界的标识符
 * （PascalCase 化后既失去机械比对能力，可读性也下降）。方括号内为对应的 uint16_t 取值。
 */
enum class TextureFormat : uint16_t {
    // 每元素 8 位
    R8, R8_SNORM, R8UI, R8I, STENCIL8,  // [0 - 4]

    // 每元素 16 位
    R16F, R16UI, R16I,                   // [5 - 7]
    RG8, RG8_SNORM, RG8UI, RG8I,         // [8 - 11]
    RGB565,                              // [12]
    // 实际为 32bpp，因历史原因归在此处
    RGB9_E5,                             // [13]
    RGB5_A1,                             // [14]
    RGBA4,                               // [15]
    DEPTH16,                             // [16]

    // 每元素 24 位
    RGB8, SRGB8, RGB8_SNORM, RGB8UI, RGB8I,  // [17 - 21]
    DEPTH24,                                 // [22]

    // 每元素 32 位
    R32F, R32UI, R32I,                       // [23 - 25]
    RG16F, RG16UI, RG16I,                    // [26 - 28]
    R11F_G11F_B10F,                          // [29]
    RGBA8, SRGB8_A8, RGBA8_SNORM,            // [30 - 32]
    UNUSED,                                  // 曾为 rgbm [33]
    RGB10_A2, RGBA8UI, RGBA8I,               // [34 - 36]
    DEPTH32F, DEPTH24_STENCIL8, DEPTH32F_STENCIL8,  // [37 - 39]

    // 每元素 48 位
    RGB16F, RGB16UI, RGB16I,                 // [40 - 42]

    // 每元素 64 位
    RG32F, RG32UI, RG32I,                    // [43 - 45]
    RGBA16F, RGBA16UI, RGBA16I,              // [46 - 48]

    // 每元素 96 位
    RGB32F, RGB32UI, RGB32I,                 // [49 - 51]

    // 每元素 128 位
    RGBA32F, RGBA32UI, RGBA32I,              // [52 - 54]

    // 压缩格式

    // GLES 3.0 与 GL 4.3 强制支持
    EAC_R11, EAC_R11_SIGNED, EAC_RG11, EAC_RG11_SIGNED,  // [55 - 58]
    ETC2_RGB8, ETC2_SRGB8,                               // [59 - 60]
    ETC2_RGB8_A1, ETC2_SRGB8_A1,                         // [61 - 62]
    ETC2_EAC_RGBA8, ETC2_EAC_SRGBA8,                     // [63 - 64]

    // 除 Android/iOS 外均可用
    DXT1_RGB, DXT1_RGBA, DXT3_RGBA, DXT5_RGBA,           // [65 - 68]
    DXT1_SRGB, DXT1_SRGBA, DXT3_SRGBA, DXT5_SRGBA,       // [69 - 72]

    // ASTC 需 GLES 扩展
    RGBA_ASTC_4x4,              // [73]
    RGBA_ASTC_5x4,              // [74]
    RGBA_ASTC_5x5,              // [75]
    RGBA_ASTC_6x5,              // [76]
    RGBA_ASTC_6x6,              // [77]
    RGBA_ASTC_8x5,              // [78]
    RGBA_ASTC_8x6,              // [79]
    RGBA_ASTC_8x8,              // [80]
    RGBA_ASTC_10x5,             // [81]
    RGBA_ASTC_10x6,             // [82]
    RGBA_ASTC_10x8,             // [83]
    RGBA_ASTC_10x10,            // [84]
    RGBA_ASTC_12x10,            // [85]
    RGBA_ASTC_12x12,            // [86]
    SRGB8_ALPHA8_ASTC_4x4,      // [87]
    SRGB8_ALPHA8_ASTC_5x4,      // [88]
    SRGB8_ALPHA8_ASTC_5x5,      // [89]
    SRGB8_ALPHA8_ASTC_6x5,      // [90]
    SRGB8_ALPHA8_ASTC_6x6,      // [91]
    SRGB8_ALPHA8_ASTC_8x5,      // [92]
    SRGB8_ALPHA8_ASTC_8x6,      // [93]
    SRGB8_ALPHA8_ASTC_8x8,      // [94]
    SRGB8_ALPHA8_ASTC_10x5,     // [95]
    SRGB8_ALPHA8_ASTC_10x6,     // [96]
    SRGB8_ALPHA8_ASTC_10x8,     // [97]
    SRGB8_ALPHA8_ASTC_10x10,    // [98]
    SRGB8_ALPHA8_ASTC_12x10,    // [99]
    SRGB8_ALPHA8_ASTC_12x12,    // [100]

    // RGTC 需 GLES 扩展
    RED_RGTC1,               // BC4 无符号 [101]
    SIGNED_RED_RGTC1,        // BC4 有符号 [102]
    RED_GREEN_RGTC2,         // BC5 无符号 [103]
    SIGNED_RED_GREEN_RGTC2,  // BC5 有符号 [104]

    // BPTC 需 GLES 扩展
    RGB_BPTC_SIGNED_FLOAT,    // BC6H 有符号 [105]
    RGB_BPTC_UNSIGNED_FLOAT,  // BC6H 无符号 [106]
    RGBA_BPTC_UNORM,          // BC7 [107]
    SRGB_ALPHA_BPTC_UNORM,    // BC7 sRGB [108]
};

// 清空颜色附件时据附件格式选择 VkClearColorValue 的 union 分支，选错即静默写坏像素
constexpr bool IsUnsignedIntFormat(TextureFormat format) noexcept {
    switch (format) {
        case TextureFormat::R8UI:
        case TextureFormat::R16UI:
        case TextureFormat::R32UI:
        case TextureFormat::RG8UI:
        case TextureFormat::RG16UI:
        case TextureFormat::RG32UI:
        case TextureFormat::RGB8UI:
        case TextureFormat::RGB16UI:
        case TextureFormat::RGB32UI:
        case TextureFormat::RGBA8UI:
        case TextureFormat::RGBA16UI:
        case TextureFormat::RGBA32UI:
            return true;
        default:
            return false;
    }
}

constexpr bool IsSignedIntFormat(TextureFormat format) noexcept {
    switch (format) {
        case TextureFormat::R8I:
        case TextureFormat::R16I:
        case TextureFormat::R32I:
        case TextureFormat::RG8I:
        case TextureFormat::RG16I:
        case TextureFormat::RG32I:
        case TextureFormat::RGB8I:
        case TextureFormat::RGB16I:
        case TextureFormat::RGB32I:
        case TextureFormat::RGBA8I:
        case TextureFormat::RGBA16I:
        case TextureFormat::RGBA32I:
            return true;
        default:
            return false;
    }
}

//! 纹理用途位掩码
enum class TextureUsage : uint16_t {
    NONE               = 0x0000,
    COLOR_ATTACHMENT   = 0x0001,  //!< 可作颜色附件
    DEPTH_ATTACHMENT   = 0x0002,  //!< 可作深度附件
    STENCIL_ATTACHMENT = 0x0004,  //!< 可作模板附件
    UPLOADABLE         = 0x0008,  //!< 可上传数据（默认）
    SAMPLEABLE         = 0x0010,  //!< 可被采样（默认）
    SUBPASS_INPUT      = 0x0020,  //!< 可作子通道输入
    BLIT_SRC           = 0x0040,  //!< 可作为 blit 源
    BLIT_DST           = 0x0080,  //!< 可作为 blit 目标
    PROTECTED          = 0x0100,  //!< 可用于受保护内容
    GEN_MIPMAPPABLE    = 0x0200,  //!< 可用于 generateMipmaps()

    DEFAULT         = UPLOADABLE | SAMPLEABLE,
    ALL_ATTACHMENTS = COLOR_ATTACHMENT | DEPTH_ATTACHMENT | STENCIL_ATTACHMENT | SUBPASS_INPUT,
};

constexpr TextureUsage operator|(TextureUsage lhs, TextureUsage rhs) noexcept {
    return static_cast<TextureUsage>(static_cast<uint16_t>(lhs) | static_cast<uint16_t>(rhs));
}

constexpr TextureUsage operator&(TextureUsage lhs, TextureUsage rhs) noexcept {
    return static_cast<TextureUsage>(static_cast<uint16_t>(lhs) & static_cast<uint16_t>(rhs));
}

// 取补用于「只含某类标志」的判定（如 transient attachment 要求用途中只有附件标志）
constexpr TextureUsage operator~(TextureUsage value) noexcept {
    return static_cast<TextureUsage>(static_cast<uint16_t>(~static_cast<uint16_t>(value)));
}

// 任一标志命中即真；NONE 恰为 0，故不提供 operator bool
constexpr bool HasAnyFlag(TextureUsage value, TextureUsage flags) noexcept {
    return static_cast<uint16_t>(value & flags) != 0;
}

//! 纹理分量重排
enum class TextureSwizzle : uint8_t {
    SubstituteZero,
    SubstituteOne,
    Channel0,
    Channel1,
    Channel2,
    Channel3,
};

//! cubemap 面；取值不可改动
enum class TextureCubemapFace : uint8_t {
    POSITIVE_X = 0,  //!< +x 面
    NEGATIVE_X = 1,  //!< -x 面
    POSITIVE_Y = 2,  //!< +y 面
    NEGATIVE_Y = 3,  //!< -y 面
    POSITIVE_Z = 4,  //!< +z 面
    NEGATIVE_Z = 5,  //!< -z 面
};

//! 采样器环绕模式
enum class SamplerWrapMode : uint8_t {
    ClampToEdge,     //!< 边缘像素向外无限延伸
    Repeat,          //!< 在环绕方向上无限重复
    MirroredRepeat,  //!< 在环绕方向上无限重复并镜像
};

//! 采样器缩小过滤；取值不可改动
enum class SamplerMinFilter : uint8_t {
    Nearest               = 0,  //!< 不做过滤，取最近邻
    Linear                = 1,  //!< 盒式过滤，4 邻域加权平均
    NearestMipmapNearest  = 2,  //!< 启用 mipmap，但不做过滤
    LinearMipmapNearest   = 3,  //!< 在单个 mipmap 层内做盒式过滤
    NearestMipmapLinear   = 4,  //!< mipmap 层之间插值，层内不做其他过滤
    LinearMipmapLinear    = 5,  //!< mipmap 层插值 + 线性过滤
};

//! 采样器放大过滤；取值不可改动
enum class SamplerMagFilter : uint8_t {
    Nearest = 0,  //!< 不做过滤，取最近邻
    Linear  = 1,  //!< 盒式过滤，4 邻域加权平均
};

//! 采样器比较模式；取值不可改动
enum class SamplerCompareMode : uint8_t {
    None             = 0,
    CompareToTexture = 1,
};

//! 深度/模板采样器的比较函数；取值不可改动
enum class SamplerCompareFunc : uint8_t {
    Le = 0,  //!< 小于等于
    Ge,      //!< 大于等于
    L,       //!< 严格小于
    G,       //!< 严格大于
    E,       //!< 等于
    Ne,      //!< 不等于
    A,       //!< 总是通过，深度/模板测试被禁用
    N,       //!< 从不通过，深度/模板测试总是失败
};

//! 采样器参数；位域布局须与上游逐位对齐——它被采样器缓存用作哈希键
struct SamplerParams {
    SamplerMagFilter   filterMag      : 1;  //!< 放大过滤（Nearest）
    SamplerMinFilter   filterMin      : 3;  //!< 缩小过滤（Nearest）
    SamplerWrapMode    wrapS          : 2;  //!< s 方向环绕模式（ClampToEdge）
    SamplerWrapMode    wrapT          : 2;  //!< t 方向环绕模式（ClampToEdge）

    SamplerWrapMode    wrapR          : 2;  //!< r 方向环绕模式（ClampToEdge）
    uint8_t            anisotropyLog2 : 3;  //!< 各向异性等级（0）
    SamplerCompareMode compareMode    : 1;  //!< 比较模式（None）
    uint8_t            padding0       : 2;  //!< 保留，必须为 0

    SamplerCompareFunc compareFunc    : 3;  //!< 比较函数（Le）
    uint8_t            padding1       : 5;  //!< 保留，必须为 0
    uint8_t            padding2       : 8;  //!< 保留，必须为 0

    struct Hasher {
        size_t operator()(SamplerParams p) const noexcept {
            // 直接按位读取：padding 必须为 0 才保证同一逻辑值位模式唯一
            return *reinterpret_cast<uint32_t const*>(reinterpret_cast<char const*>(&p));
        }
    };

    struct EqualTo {
        bool operator()(SamplerParams lhs, SamplerParams rhs) const noexcept {
            assert_invariant(lhs.padding0 == 0);
            assert_invariant(lhs.padding1 == 0);
            assert_invariant(lhs.padding2 == 0);
            auto* pLhs = reinterpret_cast<uint32_t const*>(reinterpret_cast<char const*>(&lhs));
            auto* pRhs = reinterpret_cast<uint32_t const*>(reinterpret_cast<char const*>(&rhs));
            return *pLhs == *pRhs;
        }
    };

    struct LessThan {
        bool operator()(SamplerParams lhs, SamplerParams rhs) const noexcept {
            assert_invariant(lhs.padding0 == 0);
            assert_invariant(lhs.padding1 == 0);
            assert_invariant(lhs.padding2 == 0);
            auto* pLhs = reinterpret_cast<uint32_t const*>(reinterpret_cast<char const*>(&lhs));
            auto* pRhs = reinterpret_cast<uint32_t const*>(reinterpret_cast<char const*>(&rhs));
            return *pLhs < *pRhs;
        }
    };

    NODISCARD bool IsFiltered() const noexcept {
        return filterMag != SamplerMagFilter::Nearest || filterMin != SamplerMinFilter::Nearest;
    }

private:
    friend bool operator==(SamplerParams lhs, SamplerParams rhs) noexcept { return EqualTo{}(lhs, rhs); }
    friend bool operator!=(SamplerParams lhs, SamplerParams rhs) noexcept { return !EqualTo{}(lhs, rhs); }
    friend bool operator<(SamplerParams lhs, SamplerParams rhs) noexcept { return LessThan{}(lhs, rhs); }
};

static_assert(sizeof(SamplerParams) == 4);

// 与 JNI 侧把 SamplerParams 塞进 64 位的约定一致
static_assert(sizeof(SamplerParams) <= sizeof(uint64_t), "SamplerParams must be no more than 64 bits");

//! 描述符集布局：标签 + 绑定列表
struct DescriptorSetLayout {
    NS_UTILS::String                           label;
    std::vector<DescriptorSetLayoutDescriptor> descriptors;
};

//! 混合方程
enum class BlendEquation : uint8_t {
    Add,              //!< 片元与颜色缓冲相加
    Subtract,         //!< 从颜色缓冲减去片元
    ReverseSubtract,  //!< 从片元减去颜色缓冲
    Min,              //!< 取片元与颜色缓冲的较小值
    Max,              //!< 取片元与颜色缓冲的较大值
};

//! 混合函数
enum class BlendFunction : uint8_t {
    Zero,                //!< f(src, dst) = 0
    One,                 //!< f(src, dst) = 1
    SrcColor,            //!< f(src, dst) = src
    OneMinusSrcColor,    //!< f(src, dst) = 1-src
    DstColor,            //!< f(src, dst) = dst
    OneMinusDstColor,    //!< f(src, dst) = 1-dst
    SrcAlpha,            //!< f(src, dst) = src.a
    OneMinusSrcAlpha,    //!< f(src, dst) = 1-src.a
    DstAlpha,            //!< f(src, dst) = dst.a
    OneMinusDstAlpha,    //!< f(src, dst) = 1-dst.a
    SrcAlphaSaturate,    //!< f(src, dst) = (1,1,1) * min(src.a, 1 - dst.a), 1
};

//! 模板操作
enum class StencilOperation : uint8_t {
    Keep,      //!< 保持当前值
    Zero,      //!< 置 0
    Replace,   //!< 替换为模板参考值
    Incr,      //!< 递增，超过无符号上限则钳制
    IncrWrap,  //!< 递增，超过无符号上限则回绕到 0
    Decr,      //!< 递减，到 0 则钳制
    DecrWrap,  //!< 递减，到 0 则回绕到无符号上限
    Invert,    //!< 按位取反
};

//! 模板面
enum class StencilFace : uint8_t {
    FRONT          = 0x1,           //!< 更新正面多边形的模板状态
    BACK           = 0x2,           //!< 更新背面多边形的模板状态
    FRONT_AND_BACK = FRONT | BACK,  //!< 更新所有多边形的模板状态
};

constexpr StencilFace operator|(StencilFace lhs, StencilFace rhs) noexcept {
    return static_cast<StencilFace>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

constexpr StencilFace operator&(StencilFace lhs, StencilFace rhs) noexcept {
    return static_cast<StencilFace>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
}

// 任一标志命中即真；无零值成员，但保持与其余位标志枚举一致的判定入口
constexpr bool HasAnyFlag(StencilFace value, StencilFace flags) noexcept {
    return static_cast<uint8_t>(value & flags) != 0;
}

//! 外部纹理的流类型
enum class StreamType {
    Native,    //!< 不同步但零拷贝，适合视频
    Acquired,  //!< 同步、零拷贝并接收释放回调，适合 AR，需要 API 26+
};

//! ACQUIRED 外部纹理的释放回调，保证在应用线程调用
using StreamCallback = void (*)(void* image, void* user);

/**
 * 帧调度回执的载体。
 *
 * 上游由 PresentCallable 承载（本项目未移植），而驱动侧对 setFrameScheduledCallback 的实现是
 * 空实现，故这里只保留可移动的空类型占位，仅用于满足驱动接口签名。
 */
class FrameScheduledCallback {
public:
    FrameScheduledCallback() noexcept                                     = default;
    FrameScheduledCallback(FrameScheduledCallback const&) noexcept        = default;
    FrameScheduledCallback(FrameScheduledCallback&&) noexcept             = default;
    FrameScheduledCallback& operator=(FrameScheduledCallback const&) noexcept = default;
    FrameScheduledCallback& operator=(FrameScheduledCallback&&) noexcept      = default;
    ~FrameScheduledCallback() noexcept                                     = default;
};

/**
 * 光栅化状态；以 uint32_t 位域打包，尺寸即契约
 */
struct RasterState {
    using CullingMode   = Backend::CullingMode;
    using DepthFunc     = Backend::SamplerCompareFunc;
    using BlendEquation = Backend::BlendEquation;
    using BlendFunction = Backend::BlendFunction;

    RasterState() noexcept {
        static_assert(sizeof(RasterState) == sizeof(uint32_t), "RasterState size not what was intended");
        culling               = CullingMode::Back;
        blendEquationRGB      = BlendEquation::Add;
        blendEquationAlpha    = BlendEquation::Add;
        blendFunctionSrcRGB   = BlendFunction::One;
        blendFunctionSrcAlpha = BlendFunction::One;
        blendFunctionDstRGB   = BlendFunction::Zero;
        blendFunctionDstAlpha = BlendFunction::Zero;
    }

    bool operator==(RasterState rhs) const noexcept { return u == rhs.u; }
    bool operator!=(RasterState rhs) const noexcept { return u != rhs.u; }

    void DisableBlending() noexcept {
        blendEquationRGB      = BlendEquation::Add;
        blendEquationAlpha    = BlendEquation::Add;
        blendFunctionSrcRGB   = BlendFunction::One;
        blendFunctionSrcAlpha = BlendFunction::One;
        blendFunctionDstRGB   = BlendFunction::Zero;
        blendFunctionDstAlpha = BlendFunction::Zero;
    }

    // 用于判断硬件是否需要开启混合；编译后等价于一次 load/mask/compare
    NODISCARD bool HasBlending() const noexcept {
        return !(blendEquationRGB == BlendEquation::Add && blendEquationAlpha == BlendEquation::Add &&
                 blendFunctionSrcRGB == BlendFunction::One && blendFunctionSrcAlpha == BlendFunction::One &&
                 blendFunctionDstRGB == BlendFunction::Zero && blendFunctionDstAlpha == BlendFunction::Zero);
    }

    union {
        struct {
            CullingMode   culling                 : 2;  //  2
            BlendEquation blendEquationRGB        : 3;  //  5
            BlendEquation blendEquationAlpha      : 3;  //  8
            BlendFunction blendFunctionSrcRGB     : 4;  // 12
            BlendFunction blendFunctionSrcAlpha   : 4;  // 16
            BlendFunction blendFunctionDstRGB     : 4;  // 20
            BlendFunction blendFunctionDstAlpha   : 4;  // 24
            bool          depthWrite              : 1;  // 25
            DepthFunc     depthFunc               : 3;  // 28
            bool          colorWrite              : 1;  // 29
            bool          alphaToCoverage         : 1;  // 30
            bool          inverseFrontFaces       : 1;  // 31
            bool          depthClamp              : 1;  // 32
        };
        uint32_t u = 0;
    };
};

/**
 * 渲染通道开始/结束时清空或丢弃哪些缓冲区
 */
struct RenderPassFlags {
    TargetBufferFlags clear;         //!< 开始时要清空的缓冲区，隐含 discard
    TargetBufferFlags discardStart;  //!< 开始时要丢弃的缓冲区，内容未初始化，必须被完全绘制或清空
    TargetBufferFlags discardEnd;    //!< 结束时要丢弃的缓冲区，内容失效，不可再读取
};

// 颜色附件的清空值。实际类型族（float / 有符号整数 / 无符号整数）在清空时由附件的
// TextureFormat 推断，double 被原样转换成对应的原生调用。调用方须放入对附件类型族
// 有意义的值（如 UINT 附件放 [0, UINT32_MAX]）；double 有 53 位尾数，int32/uint32 可精确往返。
using ClearColorValue = math::double4;

/**
 * 渲染通道参数
 */
struct RenderPassParams {
    RenderPassFlags flags{};       //!< 本通道对缓冲区执行的操作
    Viewport        viewport{};    //!< 本通道的视口
    DepthRange      depthRange{};  //!< 本通道的深度范围

    // 用于清空颜色附件，须同时置位 RenderPassFlags::clear
    ClearColorValue clearColor{};

    double   clearDepth   = 0.0;  //!< 深度缓冲清空值
    uint32_t clearStencil = 0;    //!< 模板缓冲清空值

    // 子通道掩码：指定哪些颜色附件在第二个子通道中回读。为 0 表示只有一个子通道；
    // 最低位对应第一个颜色附件。目前只支持 2 个子通道，故仅用低 8 位（每个颜色附件一位）。
    uint16_t subpassMask = 0;

    // 向驱动承诺深度附件（位 0）与模板附件（位 1）只读；部分后端需要据此允许对深度附件采样
    uint16_t readOnlyDepthStencil = 0;

    static constexpr uint16_t READONLY_DEPTH   = 1 << 0;
    static constexpr uint16_t READONLY_STENCIL = 1 << 1;
};

struct PolygonOffset {
    float slope    = 0;  //!< GL 语义的 factor
    float constant = 0;  //!< GL 语义的 units
};

struct StencilState {
    using StencilFunction = SamplerCompareFunc;

    struct StencilOperations {
        StencilFunction  stencilFunc                : 3;  //  3，模板测试函数
        StencilOperation stencilOpStencilFail       : 3;  //  6，模板测试失败时的操作
        uint8_t          padding0                   : 2;  //  8

        StencilOperation stencilOpDepthFail         : 3;  // 11，模板通过但深度失败时的操作
        StencilOperation stencilOpDepthStencilPass  : 3;  // 14，模板与深度均通过时的操作
        uint8_t          padding1                   : 2;  // 16

        uint8_t          ref;                         // 24，模板比较与更新的参考值
        uint8_t          readMask;                    // 32，参与比较的模板位
        uint8_t          writeMask;                   // 40，被模板测试更新的位
    };

    StencilOperations front = {
        .stencilFunc               = StencilFunction::A,
        .stencilOpStencilFail      = StencilOperation::Keep,
        .padding0                  = 0,
        .stencilOpDepthFail        = StencilOperation::Keep,
        .stencilOpDepthStencilPass = StencilOperation::Keep,
        .padding1                  = 0,
        .ref                       = 0,
        .readMask                  = 0xff,
        .writeMask                 = 0xff,
    };

    StencilOperations back = {
        .stencilFunc               = StencilFunction::A,
        .stencilOpStencilFail      = StencilOperation::Keep,
        .padding0                  = 0,
        .stencilOpDepthFail        = StencilOperation::Keep,
        .stencilOpDepthStencilPass = StencilOperation::Keep,
        .padding1                  = 0,
        .ref                       = 0,
        .readMask                  = 0xff,
        .writeMask                 = 0xff,
    };

    bool    stencilWrite = false;  //!< 是否写入模板缓冲
    uint8_t padding      = 0;
};

using PushConstantVariant = std::variant<int32_t, float, bool>;

static_assert(sizeof(StencilState::StencilOperations) == 5u, "StencilOperations size not what was intended");

static_assert(sizeof(StencilState) == 12u, "StencilState size not what was intended");

/**
 * 驱动侧针对性绕过：每项都对应一个已确认的驱动/编译器缺陷
 */
enum class Workaround : uint16_t {
    SplitEasu,                               // EASU 通道必须拆分，否则着色器编译器会把提前返回的分支抹平
    AllowReadOnlyAncillaryFeedbackLoop,      // 允许与辅助缓冲（深度/模板）构成反馈环，只要整个渲染通道内它们是只读的
    AdrenoUniformArrayCrash,                 // Adreno 上某些 uniform 数组必须做初始化，否则崩溃
    // 绕过 Metal 流水线编译错误 "Could not statically determine the target of a texture"
    MetalStaticTextureTargetError,
    DisableBlitIntoTextureArray,             // Adreno 驱动有时无法 blit 到纹理数组的某一层
    PowerVrShaderWorkarounds,                // PowerVR GPU 需要的一组绕过
    DisableDepthPrecacheForDefaultMaterial,  // Firefox on Mac 编译默认材质的程序过慢导致启动卡顿，故不预编译其深度变体
    EmulateSrgbSwapchain,                    // 在着色器里模拟 sRGB 交换链
};

using AsyncCallId = uint32_t;

struct PipelineLayout {
    using SetLayout = std::array<DescriptorSetLayoutHandle, MAX_DESCRIPTOR_SET_COUNT>;

    SetLayout setLayout;
};

struct PipelineState {
    ProgramHandle          program;
    VertexBufferInfoHandle vertexBufferInfo;
    PipelineLayout         pipelineLayout;
    RasterState            rasterState;
    StencilState           stencilState;
    PolygonOffset          polygonOffset;
    PrimitiveType          primitiveType = PrimitiveType::TRIANGLES;
    uint8_t                padding[3]    = {};
};

END_NS_BACKEND
