#include "vulkan/VulkanPipelineCache.h"

#include "JobSystem.h"
#include "vulkan/VulkanConstants.h"
#include "vulkan/VulkanHandle.h"
#include "vulkan/utils/Conversion.h"

#include "Utils/Log.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>

BEGIN_NS_BACKEND

VulkanPipelineCache::VulkanPipelineCache(DriverBase& driver, VkDevice device, VulkanContext const& context)
    : m_device(device), m_callbackManager(driver), m_context(context) {
    VkPipelineCacheCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
    };
    vkCreatePipelineCache(m_device, &createInfo, kVkAlloc, &m_pipelineCache);

    if (m_context.IsPipelineCachePrewarmingEnabled()) {
        m_compilerThreadPool.Init(
            /*threadCount=*/1,
            []() {
                JobSystem::SetThreadName("CompilerThreadPool");
                // 该线程优先级须低于主线程
                JobSystem::SetThreadPriority(JobSystem::Priority::Display);
            },
            []() {
                // 无需清理
            });
    }
}

void VulkanPipelineCache::BindLayout(VkPipelineLayout layout) noexcept { m_pipelineRequirements.layout = layout; }

VulkanPipelineCache::PipelineCacheEntry* VulkanPipelineCache::GetOrCreatePipeline() noexcept {
    // 命中则复用，否则新建
    if (auto const iter = m_pipelines.find(m_pipelineRequirements); iter != m_pipelines.end()) {
        auto& pipeline     = iter->second;
        pipeline.lastUsed  = m_currentTime;
        return &pipeline;
    }
    PipelineCacheEntry cacheEntry{
        .handle   = CreatePipeline(m_pipelineRequirements),
        .lastUsed = m_currentTime,
    };
    LOG_ASSERT(cacheEntry.handle != VK_NULL_HANDLE);
    return &m_pipelines.emplace(m_pipelineRequirements, cacheEntry).first->second;
}

void VulkanPipelineCache::BindPipeline(VulkanCommandBuffer* commands) {
    VkCommandBuffer const cmdBuffer  = commands->Buffer();
    PipelineCacheEntry*   cacheEntry = GetOrCreatePipeline();

    // 出错时交给上层优雅处理
    LOG_ASSERT(cacheEntry != nullptr);

    static PipelineEqual equal;
    if (!equal(m_boundPipeline, m_pipelineRequirements)) {
        m_boundPipeline = m_pipelineRequirements;
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, cacheEntry->handle);
    }
}

void VulkanPipelineCache::AsyncPrewarmCache(VulkanProgramPtr vprogram, VkPipelineLayout layout, StereoscopicType stereoscopicType,
                                            uint8_t stereoscopicViewCount, CompilerPriorityQueue priority) {
    PipelineKey key{
        .shaders = {
            vprogram->GetVertexShader(),
            vprogram->GetFragmentShader(),
        },
        // 走动态渲染路径，故渲染通道须为空
        .renderPass    = VK_NULL_HANDLE,
        .topology      = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .subpassIndex  = 0,
        // 走顶点输入动态状态，故这两组描述须为空
        .vertexAttributes = {},
        .vertexBuffers    = {},
        // 取一组合理的默认光栅状态即可：这里假定该管线不会被真正复用
        .rasterState = {
            .cullMode                = VK_CULL_MODE_NONE,
            .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable         = VK_FALSE,
            .blendEnable             = VK_FALSE,
            .depthWriteEnable        = VK_FALSE,
            .alphaToCoverageEnable   = VK_FALSE,
            .srcColorBlendFactor     = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor     = VK_BLEND_FACTOR_ONE,
            .srcAlphaBlendFactor     = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor     = VK_BLEND_FACTOR_ONE,
            .colorWriteMask          = 0,
            .rasterizationSamples    = VK_SAMPLE_COUNT_1_BIT,
            .depthClamp              = VK_FALSE,
            .colorTargetCount        = 1,
            .colorBlendOp            = BlendEquation::Subtract,
            .alphaBlendOp            = BlendEquation::Subtract,
            .depthCompareOp          = SamplerCompareFunc::L,
            .depthBiasConstantFactor = 0.f,
            .depthBiasSlopeFactor    = 0.f,
        },
        .stencilState = {},
        .layout       = layout,
    };
    PipelineDynamicOptions dynamicOptions{
        .useDynamicVertexInputState = true,
        .useDynamicRenderPasses     = true,
        .stereoscopicType           = stereoscopicType,
        .stereoscopicViewCount      = stereoscopicViewCount,
    };

    CallbackManager::Handle cmh   = m_callbackManager.Get();
    auto                    token = std::make_shared<ProgramToken>();
    // 任务里持有一份 vprogram 引用：着色器模块不能在 createPipeline 之前销毁。
    // 程序已销毁的情况靠取消标记识别，避免编译无用的材质
    m_compilerThreadPool.Queue(priority, token, [this, vprogram, key, dynamicOptions, cmh]() mutable {
        if (vprogram->IsParallelCompilationCanceled()) {
            LOG_DEBUG("Skipping prewarm for a program that has been destroyed already.");
            return;
        }

        VkPipeline const pipeline = CreatePipeline(key, dynamicOptions);
        m_callbackManager.Put(cmh);
        // 这里并不需要这条管线，只是借创建过程让驱动把管线信息缓存起来
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, pipeline, kVkAlloc);
        } else {
            LOG_WARN("Failed to create a pipeline during prewarming, draw-time pipeline creation may fail.");
        }
    });
}

VkPipeline VulkanPipelineCache::CreatePipeline(PipelineKey const& key, PipelineDynamicOptions const& dynamicOptions) noexcept {
    LOG_ASSERT(key.shaders[0] && "Vertex shader is not bound.");
    LOG_ASSERT(key.layout && "No pipeline layout specified");

    VkPipelineShaderStageCreateInfo shaderStages[kShaderModuleCount];
    shaderStages[0] = {
        .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage  = VK_SHADER_STAGE_VERTEX_BIT,
        .module = key.shaders[0],
        .pName  = "main",
    };
    shaderStages[1]          = shaderStages[0];
    shaderStages[1].stage    = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module   = key.shaders[1];

    bool const hasFragmentShader = shaderStages[1].module != VK_NULL_HANDLE;

    VkPipelineColorBlendAttachmentState colorBlendAttachments[MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT];
    VkPipelineColorBlendStateCreateInfo colorBlendState = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = key.rasterState.colorTargetCount,
        .pAttachments    = colorBlendAttachments,
    };

    VkPipelineVertexInputStateCreateInfo vertexInputState;
    VkVertexInputAttributeDescription    vertexAttributes[kVertexAttributeCount];
    VkVertexInputBindingDescription      vertexBuffers[kVertexAttributeCount];
    if (!dynamicOptions.useDynamicVertexInputState) {
        uint32_t numVertexAttribs = 0;
        uint32_t numVertexBuffers = 0;

        for (uint32_t i = 0; i < kVertexAttributeCount; i++) {
            if (key.vertexAttributes[i].format > 0) {
                vertexAttributes[numVertexAttribs++] = key.vertexAttributes[i];
            }
            if (key.vertexBuffers[i].stride > 0) {
                vertexBuffers[numVertexBuffers++] = key.vertexBuffers[i];
            }
        }
        vertexInputState = {
            .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount   = numVertexBuffers,
            .pVertexBindingDescriptions      = vertexBuffers,
            .vertexAttributeDescriptionCount = numVertexAttribs,
            .pVertexAttributeDescriptions    = vertexAttributes,
        };
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {
        .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = static_cast<VkPrimitiveTopology>(key.topology),
    };
    VkPipelineViewportStateCreateInfo viewportState = {
        .sType        = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount  = 1,
    };

    constexpr size_t kMaxDynamicStates = 3;
    size_t           numDynamicStates  = 2;
    VkDynamicState   enabledDynamicStates[kMaxDynamicStates] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    if (dynamicOptions.useDynamicVertexInputState) {
        enabledDynamicStates[numDynamicStates++] = VK_DYNAMIC_STATE_VERTEX_INPUT_EXT;
    }
    VkPipelineDynamicStateCreateInfo dynamicState = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(numDynamicStates),
        .pDynamicStates    = enabledDynamicStates,
    };

    auto const&                          raster = key.rasterState;
    VkPipelineRasterizationStateCreateInfo vkRaster = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable        = raster.depthClamp,
        .polygonMode             = VK_POLYGON_MODE_FILL,
        .cullMode                = raster.cullMode,
        .frontFace               = raster.frontFace,
        .depthBiasEnable         = raster.depthBiasEnable,
        .depthBiasConstantFactor = raster.depthBiasConstantFactor,
        .depthBiasClamp          = 0.0f,
        .depthBiasSlopeFactor    = raster.depthBiasSlopeFactor,
        .lineWidth               = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo vkMs = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples  = static_cast<VkSampleCountFlagBits>(raster.rasterizationSamples),
        .sampleShadingEnable   = VK_FALSE,
        .minSampleShading      = 0.0f,
        .alphaToCoverageEnable = raster.alphaToCoverageEnable,
        .alphaToOneEnable      = VK_FALSE,
    };
    bool const enableDepthTest = raster.depthCompareOp != SamplerCompareFunc::A || raster.depthWriteEnable;
    // 做模板测试或写模板都要开启模板测试
    auto const& stencil            = key.stencilState;
    bool const  enableStencilTest  = stencil.front.stencilFunc != StencilState::StencilFunction::A ||
                                   stencil.back.stencilFunc != StencilState::StencilFunction::A ||
                                   stencil.front.stencilOpDepthFail != StencilOperation::Keep ||
                                   stencil.back.stencilOpDepthFail != StencilOperation::Keep ||
                                   stencil.front.stencilOpStencilFail != StencilOperation::Keep ||
                                   stencil.back.stencilOpStencilFail != StencilOperation::Keep ||
                                   stencil.front.stencilOpDepthStencilPass != StencilOperation::Keep ||
                                   stencil.back.stencilOpDepthStencilPass != StencilOperation::Keep || stencil.stencilWrite;
    VkPipelineDepthStencilStateCreateInfo vkDs = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable       = enableDepthTest ? VK_TRUE : VK_FALSE,
        .depthWriteEnable      = raster.depthWriteEnable,
        .depthCompareOp        = VK_UTILS::TransSamplerCompareFuncToVkCompareOp(raster.depthCompareOp),
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable     = enableStencilTest ? VK_TRUE : VK_FALSE,
        .minDepthBounds        = 0.0f,
        .maxDepthBounds        = 0.0f,
    };
    vkDs.front = {
        .failOp      = VK_UTILS::TransStencilOperationToVkStencilOp(stencil.front.stencilOpStencilFail),
        .passOp      = VK_UTILS::TransStencilOperationToVkStencilOp(stencil.front.stencilOpDepthStencilPass),
        .depthFailOp = VK_UTILS::TransStencilOperationToVkStencilOp(stencil.front.stencilOpDepthFail),
        .compareOp   = VK_UTILS::TransSamplerCompareFuncToVkCompareOp(stencil.front.stencilFunc),
        .compareMask = stencil.front.readMask,
        .writeMask   = static_cast<uint32_t>(stencil.stencilWrite ? stencil.front.writeMask : 0u),
        .reference   = static_cast<uint32_t>(stencil.front.ref),
    };
    vkDs.back = {
        .failOp      = VK_UTILS::TransStencilOperationToVkStencilOp(stencil.back.stencilOpStencilFail),
        .passOp      = VK_UTILS::TransStencilOperationToVkStencilOp(stencil.back.stencilOpDepthStencilPass),
        .depthFailOp = VK_UTILS::TransStencilOperationToVkStencilOp(stencil.back.stencilOpDepthFail),
        .compareOp   = VK_UTILS::TransSamplerCompareFuncToVkCompareOp(stencil.back.stencilFunc),
        .compareMask = stencil.back.readMask,
        .writeMask   = static_cast<uint32_t>(stencil.stencilWrite ? stencil.back.writeMask : 0u),
        .reference   = static_cast<uint32_t>(stencil.back.ref),
    };

    VkGraphicsPipelineCreateInfo pipelineCreateInfo = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount          = hasFragmentShader ? kShaderModuleCount : 1,
        .pStages             = shaderStages,
        .pVertexInputState   = dynamicOptions.useDynamicVertexInputState ? nullptr : &vertexInputState,
        .pInputAssemblyState = &inputAssemblyState,
        .pViewportState      = &viewportState,
        .pRasterizationState = &vkRaster,
        .pMultisampleState   = &vkMs,
        .pDepthStencilState  = &vkDs,
        .pColorBlendState    = &colorBlendState,
        .pDynamicState       = &dynamicState,
        .layout              = key.layout,
        .renderPass          = key.renderPass,
        .subpass             = key.subpassIndex,
    };

    // 没有片元着色器就没有颜色附件（例如生成阴影贴图）
    if (!hasFragmentShader) {
        colorBlendState.attachmentCount = 0;
    } else {
        // Filament 假定所有颜色附件的混合状态一致
        colorBlendAttachments[0] = {
            .blendEnable         = raster.blendEnable,
            .srcColorBlendFactor = raster.srcColorBlendFactor,
            .dstColorBlendFactor = raster.dstColorBlendFactor,
            .colorBlendOp        = static_cast<VkBlendOp>(raster.colorBlendOp),
            .srcAlphaBlendFactor = raster.srcAlphaBlendFactor,
            .dstAlphaBlendFactor = raster.dstAlphaBlendFactor,
            .alphaBlendOp        = static_cast<VkBlendOp>(raster.alphaBlendOp),
            .colorWriteMask      = raster.colorWriteMask,
        };
        for (uint8_t i = 1; i < colorBlendState.attachmentCount; ++i) {
            colorBlendAttachments[i] = colorBlendAttachments[0];
        }
    }

    VkPipelineRenderingCreateInfoKHR renderingInfo{};
    VkFormat                         pipelineRenderingColorFormats[] = { VK_FORMAT_UNDEFINED };
    if (dynamicOptions.useDynamicRenderPasses) {
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
        renderingInfo.pNext = pipelineCreateInfo.pNext;
        // 尚无取值的字段填空值
        renderingInfo.depthAttachmentFormat   = VK_FORMAT_UNDEFINED;
        renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
        // 多视图时把每个启用的视图位置 1，否则置 0
        renderingInfo.viewMask = dynamicOptions.stereoscopicType == StereoscopicType::Multiview
                                         ? (1 << dynamicOptions.stereoscopicViewCount) - 1
                                         : 0;

        if (hasFragmentShader) {
            renderingInfo.colorAttachmentCount    = 1;
            renderingInfo.pColorAttachmentFormats = pipelineRenderingColorFormats;
        }

        pipelineCreateInfo.pNext = &renderingInfo;
    }

    VkPipeline pipeline;
    VkResult const error = vkCreateGraphicsPipelines(m_device, m_pipelineCache, 1, &pipelineCreateInfo, kVkAlloc, &pipeline);

    // 先打日志再断言：release 下断言不生效时也能看到错误码
    if (error != VK_SUCCESS) {
        LOG_ERROR("vkCreateGraphicsPipelines error {}", static_cast<int32_t>(error));
    }
    LOG_ASSERT(error == VK_SUCCESS);
    if (error != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

void VulkanPipelineCache::BindProgram(VulkanProgramPtr const& program) noexcept {
    m_pipelineRequirements.shaders[0] = program->GetVertexShader();
    m_pipelineRequirements.shaders[1] = program->GetFragmentShader();
}

void VulkanPipelineCache::BindRasterState(RasterState const& rasterState) noexcept { m_pipelineRequirements.rasterState = rasterState; }

void VulkanPipelineCache::BindStencilState(StencilState const& stencilState) noexcept { m_pipelineRequirements.stencilState = stencilState; }

void VulkanPipelineCache::BindRenderPass(VulkanRenderPassPtr const& renderPass, int subpassIndex) noexcept {
    m_pipelineRequirements.renderPass   = renderPass->GetVkRenderPass();
    m_pipelineRequirements.subpassIndex = static_cast<uint16_t>(subpassIndex);
}

void VulkanPipelineCache::BindPrimitiveTopology(VkPrimitiveTopology topology) noexcept {
    LOG_ASSERT(static_cast<uint32_t>(topology) <= 0xffffu);
    m_pipelineRequirements.topology = static_cast<uint16_t>(topology);
}

void VulkanPipelineCache::BindVertexArray(VkVertexInputAttributeDescription const* attribDesc, VkVertexInputBindingDescription const* bufferDesc,
                                          uint8_t count) {
    for (size_t i = 0; i < kVertexAttributeCount; i++) {
        if (i < count) {
            m_pipelineRequirements.vertexAttributes[i] = attribDesc[i];
            m_pipelineRequirements.vertexBuffers[i]    = bufferDesc[i];
        } else {
            m_pipelineRequirements.vertexAttributes[i] = VertexInputAttributeDescription{};
            m_pipelineRequirements.vertexBuffers[i]    = VertexInputBindingDescription{};
        }
    }
}

void VulkanPipelineCache::AddCachePrewarmCallback(CallbackHandler* handler, CallbackHandler::Callback callback, void* user) {
    if (callback) {
        m_callbackManager.SetCallback(handler, callback, user);
    }
}

void VulkanPipelineCache::ResetBoundPipeline() { m_boundPipeline = {}; }

void VulkanPipelineCache::Terminate() noexcept {
    for (auto& iter : m_pipelines) {
        vkDestroyPipeline(m_device, iter.second.handle, kVkAlloc);
    }
    m_pipelines.clear();
    ResetBoundPipeline();

    m_callbackManager.Terminate();
    m_compilerThreadPool.Terminate();

    // 幂等：以句柄是否仍有效作为守卫
    if (m_pipelineCache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(m_device, m_pipelineCache, kVkAlloc);
        m_pipelineCache = VK_NULL_HANDLE;
    }
}

void VulkanPipelineCache::Gc() noexcept {
    ++m_currentTime;

    // Vulkan 规范规定：命令缓冲开始录制时其全部状态都是未定义的。因此这里清空全部绑定
    ResetBoundPipeline();

    // 逐出长时间未使用的管线。早于 kMaxPipelineAge 次提交的条目可安全销毁。
    // 用迭代器形式删除：按 key 删除会使当前迭代器失效
    using ConstPipeIterator = PipelineMap::const_iterator;
    for (ConstPipeIterator iter = m_pipelines.begin(); iter != m_pipelines.end();) {
        PipelineCacheEntry const& cacheEntry = iter->second;
        if (cacheEntry.lastUsed + static_cast<Timestamp>(kMaxPipelineAge) < m_currentTime) {
            vkDestroyPipeline(m_device, iter->second.handle, kVkAlloc);
            iter = m_pipelines.erase(iter);
        } else {
            ++iter;
        }
    }
}

bool VulkanPipelineCache::PipelineEqual::operator()(PipelineKey const& k1, PipelineKey const& k2) const {
    return 0 == std::memcmp(static_cast<void const*>(&k1), static_cast<void const*>(&k2), sizeof(k1));
}

END_NS_BACKEND
