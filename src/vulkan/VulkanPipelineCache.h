#pragma once

#include "Backend/DriverDefine.h"
#include "Backend/TargetBufferInfo.h"

#include "Utils/Hash.h"
#include "Utils/Macro.h"
#include "Utils/Utils.h"

#include <cstdint>
#include <type_traits>
#include <unordered_map>

#include "CallbackManager.h"
#include "CompilerThreadPool.h"
#include "vulkan/VkDef.h"
#include "vulkan/VulkanAsyncHandles.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/VulkanFboCache.h"
#include "vulkan/commands/VulkanCommandBuffer.h"

BEGIN_NS_BACKEND

class DriverBase;

// 图形管线与其布局的缓存。
//
// 约定：视口与裁剪矩形一律是动态状态，不烘进 VkPipeline。
class VulkanPipelineCache {
public:
    VulkanPipelineCache(VulkanPipelineCache const&)            = delete;
    VulkanPipelineCache& operator=(VulkanPipelineCache const&) = delete;

    static constexpr uint32_t kShaderModuleCount    = 2;
    static constexpr uint32_t kVertexAttributeCount = MAX_VERTEX_ATTRIBUTE_COUNT;

    // 与管线相关、可烘焙进 VkPipeline 的固定功能状态。
    //
    // 省略了下述状态：Filament 从不改变它们（depthClampEnable / rasterizerDiscardEnable /
    // depthBoundsTestEnable / minSampleShading / alphaToOneEnable / sampleShadingEnable /
    // minDepthBounds / maxDepthBounds / depthBiasClamp / polygonMode / lineWidth）。
    //
    // 本结构以位域紧凑排布并使用整块 memcmp 比较，故字段顺序与宽度都是契约：
    // VulkanDriver::bindPipelineImpl 以指定初始化器逐字段构造它。
    struct RasterState {
        VkCullModeFlags       cullMode : 2;
        VkFrontFace           frontFace : 2;
        VkBool32              depthBiasEnable : 1;
        VkBool32              blendEnable : 1;
        VkBool32              depthWriteEnable : 1;
        VkBool32              alphaToCoverageEnable : 1;
        VkBlendFactor         srcColorBlendFactor : 5;  // offset = 1 byte
        VkBlendFactor         dstColorBlendFactor : 5;
        VkBlendFactor         srcAlphaBlendFactor : 5;
        VkBlendFactor         dstAlphaBlendFactor : 5;
        VkColorComponentFlags colorWriteMask : 4;
        uint8_t               rasterizationSamples : 4;  // offset = 4 bytes
        uint8_t               depthClamp : 4;
        uint8_t               colorTargetCount;         // offset = 5 bytes
        BlendEquation         colorBlendOp : 4;         // offset = 6 bytes
        BlendEquation         alphaBlendOp : 4;
        SamplerCompareFunc    depthCompareOp;           // offset = 7 bytes
        float                 depthBiasConstantFactor;  // offset = 8 bytes
        float                 depthBiasSlopeFactor;     // offset = 12 bytes
    };

    static_assert(std::is_trivially_copyable<RasterState>::value, "RasterState must be a POD for fast hashing.");

    static_assert(sizeof(RasterState) == 16, "RasterState must not have implicit padding.");

    // driver 仅用于构造回调管理器（回调经它调度）
    VulkanPipelineCache(DriverBase& driver, VkDevice device, VulkanContext const& context);

    // 在另一线程上创建一个「假」管线，目的是让驱动的内部管线缓存提前预热，使 draw 时的真实
    // 管线尽量命中缓存。强依赖驱动的实现，预期在支持 VK_EXT_vertex_input_dynamic_state 与
    // VK_KHR_dynamic_rendering 的设备上有效。
    void AsyncPrewarmCache(VulkanProgramPtr vprogram, VkPipelineLayout layout, StereoscopicType stereoscopicType,
                           uint8_t stereoscopicViewCount, CompilerPriorityQueue priority);

    // 全部在途的异步预热任务完成后通知回调，通常用于告知前端「此刻可以安全地在 draw 时编译管线」
    void AddCachePrewarmCallback(CallbackHandler* handler, CallbackHandler::Callback callback, void* user);

    // 需要时创建管线并用 vkCmdBindPipeline 绑定
    void BindPipeline(VulkanCommandBuffer* commands);

    // 以下方法都很快，不产生 Vulkan 调用
    void BindLayout(VkPipelineLayout layout) noexcept;
    void BindProgram(VulkanProgramPtr const& program) noexcept;
    void BindRasterState(RasterState const& rasterState) noexcept;
    void BindStencilState(StencilState const& stencilState) noexcept;
    void BindRenderPass(VulkanRenderPassPtr const& renderPass, int subpassIndex) noexcept;
    void BindPrimitiveTopology(VkPrimitiveTopology topology) noexcept;
    void BindVertexArray(VkVertexInputAttributeDescription const* attribDesc, VkVertexInputBindingDescription const* bufferDesc,
                         uint8_t count);

    // 把当前管线与描述符集绑定清空
    void ResetBoundPipeline();

    // 销毁全部受管 Vulkan 对象，须在 VkDevice 变更前调用；语义幂等
    void Terminate() noexcept;

    void Gc() noexcept;

    // 管线缓存条目。暴露出来只为让驱动与测试能查询句柄，调用方不得留存该指针
    struct PipelineCacheEntry {
        VkPipeline handle;
        uint64_t   lastUsed;
    };

    NODISCARD PipelineCacheEntry* GetOrCreatePipeline() noexcept;

    // 缓存条目数。句柄值在销毁后会被驱动复用，故逐出能否生效只能以条目数观测
    NODISCARD size_t GetPipelineCount() const noexcept { return m_pipelines.size(); }

private:
    // 管线缓存键
    //
    // 与 VkVertexInputAttributeDescription 等价但体积减半
    struct VertexInputAttributeDescription {
        VertexInputAttributeDescription& operator=(VkVertexInputAttributeDescription const& that) {
            LOG_ASSERT(that.location <= 0xffu);
            LOG_ASSERT(that.binding <= 0xffu);
            LOG_ASSERT(static_cast<uint32_t>(that.format) <= 0xffffu);
            location = static_cast<uint8_t>(that.location);
            binding  = static_cast<uint8_t>(that.binding);
            format   = static_cast<uint16_t>(that.format);
            offset   = that.offset;
            return *this;
        }

        operator VkVertexInputAttributeDescription() const {
            return { location, binding, static_cast<VkFormat>(format), offset };
        }

        uint8_t  location;
        uint8_t  binding;
        uint16_t format;
        uint32_t offset;
    };

    // 与 VkVertexInputBindingDescription 等价但更小
    struct VertexInputBindingDescription {
        VertexInputBindingDescription& operator=(VkVertexInputBindingDescription const& that) {
            LOG_ASSERT(that.binding <= 0xffffu);
            binding   = static_cast<uint16_t>(that.binding);
            stride    = that.stride;
            inputRate = static_cast<uint16_t>(that.inputRate);
            return *this;
        }

        operator VkVertexInputBindingDescription() const {
            return { binding, stride, static_cast<VkVertexInputRate>(inputRate) };
        }

        uint16_t binding;
        uint16_t inputRate;
        uint32_t stride;
    };

    // 管线缓存键是「构成不可变 VkPipeline 的全部已绑定状态」的 POD，逐字段比较
    struct PipelineKey {                                                              // size : offset
        VkShaderModule shaders[kShaderModuleCount];                                   //  16  : 0
        VkRenderPass   renderPass;                                                    //  8   : 16
        uint16_t       topology;                                                      //  2   : 24
        uint16_t       subpassIndex;                                                  //  2   : 26
        VertexInputAttributeDescription vertexAttributes[kVertexAttributeCount];      //  128 : 28
        VertexInputBindingDescription   vertexBuffers[kVertexAttributeCount];         //  128 : 156
        RasterState                     rasterState;                                  //  16  : 284
        StencilState                    stencilState;                                 //  12  : 300
        VkPipelineLayout                layout;                                       //  8   : 312
    };

    // 创建管线时可选启用的动态状态（视驱动能力而定）
    struct PipelineDynamicOptions {
        // 需要 VK_EXT_vertex_input_dynamic_state
        bool useDynamicVertexInputState = false;
        // 需要 VK_KHR_dynamic_rendering
        bool useDynamicRenderPasses = false;
        // 仅在 useDynamicRenderPasses 为 true 时有效
        StereoscopicType stereoscopicType = StereoscopicType::None;
        // 仅在 stereoscopicType 为 Multiview 时有效
        uint8_t stereoscopicViewCount = 2;
    };

    static_assert(sizeof(PipelineKey) == 320, "PipelineKey must not have implicit padding.");

    using PipelineHashFn = NS_UTILS::hash::MurmurHashFn<PipelineKey>;

    struct PipelineEqual {
        bool operator()(PipelineKey const& k1, PipelineKey const& k2) const;
    };

    // 缓存条目上的时间戳是「自缓存建立以来经过的提交次数」。若某条目的最近使用时刻距今超过
    // kMaxPipelineAge 次提交，即可确定 GPU 不再使用它，销毁是安全的。
    using Timestamp = uint64_t;

    using PipelineMap = std::unordered_map<PipelineKey, PipelineCacheEntry, PipelineHashFn, PipelineEqual>;

    // 创建一个禁用全部可选动态状态的管线
    VkPipeline CreatePipeline(PipelineKey const& key) noexcept { return CreatePipeline(key, {}); }

    // 返回的指针不稳定，不得留存
    VkPipeline CreatePipeline(PipelineKey const& key, PipelineDynamicOptions const& dynamicOptions) noexcept;

    Timestamp m_currentTime = 0;

    PipelineMap m_pipelines;

    // 不变状态
    VkDevice m_device = VK_NULL_HANDLE;

    // 驱动的管线缓存句柄。条目被 Gc 逐出后重建同一条管线时，它能让驱动复用已编译结果
    VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;

    // 当前管线、布局与描述符集的需求状态
    PipelineKey m_pipelineRequirements = {};

    // 当前已绑定的管线与描述符集状态
    PipelineKey m_boundPipeline = {};

    // 允许预热管线缓存以缩短 draw 时的管线编译时间
    CompilerThreadPool m_compilerThreadPool;

    // 让缓存管理器能在预编译任务全部完成后通知前端
    CallbackManager m_callbackManager;

    [[maybe_unused]] VulkanContext const& m_context;
};

END_NS_BACKEND
