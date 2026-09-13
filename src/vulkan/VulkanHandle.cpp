#include "VulkanHandle.h"

#include "VulkanSwapChain.h"
#include "vulkan/commands/VulkanCommandBuffer.h"
#include "vulkan/utils/Image.h"

#include "Utils/Log.h"
#include "Utils/Macro.h"

#include <algorithm>
#include <utility>

BEGIN_NS_BACKEND

namespace {

// 空槽积累到这个数量才压缩描述符集数组，避免每次绑定都搬运
constexpr size_t kDescriptorSetGcLimit = 10;

template <typename Bitmask>
void FromStageFlags(ShaderStageFlags stage, descriptor_binding_t binding, Bitmask& mask) {
    if (static_cast<bool>(stage & ShaderStageFlags::VERTEX)) {
        mask.Set(binding + VK_UTILS::GetVertexStageShift<Bitmask>());
    }
    if (static_cast<bool>(stage & ShaderStageFlags::FRAGMENT)) {
        mask.Set(binding + VK_UTILS::GetFragmentStageShift<Bitmask>());
    }
}

// 应用层用途到 buffer 域池类型的映射：两类缓冲对象共用池，各自用途决定 VkBuffer usage
VulkanBufferBinding GetBufferObjectBinding(BufferObjectBinding bindingType) noexcept {
    switch (bindingType) {
        CASE_FROM_TO(BufferObjectBinding::Vertex, VulkanBufferBinding::Vertex);
        CASE_FROM_TO(BufferObjectBinding::Uniform, VulkanBufferBinding::Uniform);
        CASE_FROM_TO(BufferObjectBinding::ShaderStorage, VulkanBufferBinding::ShaderStorage);
    }
    return VulkanBufferBinding::Unknown;
}

// MoltenVK 等平台的视口原点在左下，而驱动 API 用左上原点
void FlipVertically(VkViewport* rect, uint32_t framebufferHeight) { rect->y = static_cast<float>(framebufferHeight) - rect->y - rect->height; }

void ClampToFramebuffer(VkRect2D* rect, uint32_t fbWidth, uint32_t fbHeight) {
    // 先把原点翻到左上，再裁到帧缓冲范围内
    rect->offset.y      = static_cast<int32_t>(fbHeight) - rect->offset.y - rect->extent.height;
    int32_t const x     = std::max(rect->offset.x, 0);
    int32_t const y     = std::max(rect->offset.y, 0);
    int32_t const right = std::min(rect->offset.x + static_cast<int32_t>(rect->extent.width), static_cast<int32_t>(fbWidth));
    int32_t const top   = std::min(rect->offset.y + static_cast<int32_t>(rect->extent.height), static_cast<int32_t>(fbHeight));
    rect->offset.x      = std::min(x, static_cast<int32_t>(fbWidth));
    rect->offset.y      = std::min(y, static_cast<int32_t>(fbHeight));
    rect->extent.width  = static_cast<uint32_t>(std::max(right - x, 0));
    rect->extent.height = static_cast<uint32_t>(std::max(top - y, 0));
}

// 交换链附件的层数由纹理主视图范围决定，而非固定为 1
VulkanAttachment CreateSwapchainAttachment(const VulkanTexturePtr& texture) {
    return VulkanAttachment{
        .texture    = texture,
        .level      = 0,
        .layerCount = static_cast<uint8_t>(texture ? texture->GetPrimaryViewRange().layerCount : 1),
        .layer      = 0,
    };
}

// MSAA 侧车纹理随源纹理走：同一条纹理只会创建一个，之后复用
VulkanTexturePtr InitMsaaTexture(const VulkanTexturePtr& texture, VkDevice device, VkPhysicalDevice physicalDevice, const VulkanContextPtr& context,
                                 VmaAllocator allocator, VulkanCommands* commands, const ResourceManagerPtr& resManager, uint8_t levels,
                                 uint8_t samples, const VulkanStagePoolPtr& stagePool) {
    LOG_ASSERT(static_cast<bool>(texture));

    VulkanTexturePtr msTexture = texture->GetSidecar();
    if (!msTexture) {
        // 去掉与附件无关的用途位，才能用上 transient attachment
        TextureUsage const usage = texture->usage & TextureUsage::ALL_ATTACHMENTS;
        LOG_ASSERT(static_cast<uint16_t>(usage) != 0U);

        msTexture = resManager->AllocateAndConstruct<VulkanTexture>(device, physicalDevice, context, allocator, resManager, commands, texture->target,
                                                                    levels, texture->format, samples, texture->width, texture->height, texture->depth,
                                                                    usage, stagePool);
        texture->SetSidecar(msTexture);
    }
    return msTexture;
}

VkFormat GetVkFormat(ElementType type, bool normalized, bool integer) noexcept {
    if (normalized) {
        switch (type) {
            CASE_FROM_TO(ElementType::BYTE, VK_FORMAT_R8_SNORM);
            CASE_FROM_TO(ElementType::UBYTE, VK_FORMAT_R8_UNORM);
            CASE_FROM_TO(ElementType::SHORT, VK_FORMAT_R16_SNORM);
            CASE_FROM_TO(ElementType::USHORT, VK_FORMAT_R16_UNORM);
            CASE_FROM_TO(ElementType::BYTE2, VK_FORMAT_R8G8_SNORM);
            CASE_FROM_TO(ElementType::UBYTE2, VK_FORMAT_R8G8_UNORM);
            CASE_FROM_TO(ElementType::SHORT2, VK_FORMAT_R16G16_SNORM);
            CASE_FROM_TO(ElementType::USHORT2, VK_FORMAT_R16G16_UNORM);
            CASE_FROM_TO(ElementType::BYTE3, VK_FORMAT_R8G8B8_SNORM);
            CASE_FROM_TO(ElementType::UBYTE3, VK_FORMAT_R8G8B8_UNORM);
            CASE_FROM_TO(ElementType::SHORT3, VK_FORMAT_R16G16B16_SNORM);
            CASE_FROM_TO(ElementType::USHORT3, VK_FORMAT_R16G16B16_UNORM);
            CASE_FROM_TO(ElementType::BYTE4, VK_FORMAT_R8G8B8A8_SNORM);
            CASE_FROM_TO(ElementType::UBYTE4, VK_FORMAT_R8G8B8A8_UNORM);
            CASE_FROM_TO(ElementType::SHORT4, VK_FORMAT_R16G16B16A16_SNORM);
            CASE_FROM_TO(ElementType::USHORT4, VK_FORMAT_R16G16B16A16_UNORM);
            default:
                LOG_CRITICAL("No normalized format for ElementType {}", static_cast<int>(type));
                return VK_FORMAT_UNDEFINED;
        }
    }

    switch (type) {
        CASE_FROM_TO(ElementType::BYTE, integer ? VK_FORMAT_R8_SINT : VK_FORMAT_R8_SSCALED);
        CASE_FROM_TO(ElementType::UBYTE, integer ? VK_FORMAT_R8_UINT : VK_FORMAT_R8_USCALED);
        CASE_FROM_TO(ElementType::SHORT, integer ? VK_FORMAT_R16_SINT : VK_FORMAT_R16_SSCALED);
        CASE_FROM_TO(ElementType::USHORT, integer ? VK_FORMAT_R16_UINT : VK_FORMAT_R16_USCALED);
        CASE_FROM_TO(ElementType::HALF, VK_FORMAT_R16_SFLOAT);
        CASE_FROM_TO(ElementType::INT, VK_FORMAT_R32_SINT);
        CASE_FROM_TO(ElementType::UINT, VK_FORMAT_R32_UINT);
        CASE_FROM_TO(ElementType::FLOAT, VK_FORMAT_R32_SFLOAT);
        CASE_FROM_TO(ElementType::BYTE2, integer ? VK_FORMAT_R8G8_SINT : VK_FORMAT_R8G8_SSCALED);
        CASE_FROM_TO(ElementType::UBYTE2, integer ? VK_FORMAT_R8G8_UINT : VK_FORMAT_R8G8_USCALED);
        CASE_FROM_TO(ElementType::SHORT2, integer ? VK_FORMAT_R16G16_SINT : VK_FORMAT_R16G16_SSCALED);
        CASE_FROM_TO(ElementType::USHORT2, integer ? VK_FORMAT_R16G16_UINT : VK_FORMAT_R16G16_USCALED);
        CASE_FROM_TO(ElementType::HALF2, VK_FORMAT_R16G16_SFLOAT);
        CASE_FROM_TO(ElementType::FLOAT2, VK_FORMAT_R32G32_SFLOAT);
        CASE_FROM_TO(ElementType::BYTE3, integer ? VK_FORMAT_R8G8B8_SINT : VK_FORMAT_R8G8B8_SSCALED);
        CASE_FROM_TO(ElementType::UBYTE3, integer ? VK_FORMAT_R8G8B8_UINT : VK_FORMAT_R8G8B8_USCALED);
        CASE_FROM_TO(ElementType::SHORT3, integer ? VK_FORMAT_R16G16B16_SINT : VK_FORMAT_R16G16B16_SSCALED);
        CASE_FROM_TO(ElementType::USHORT3, integer ? VK_FORMAT_R16G16B16_UINT : VK_FORMAT_R16G16B16_USCALED);
        CASE_FROM_TO(ElementType::HALF3, VK_FORMAT_R16G16B16_SFLOAT);
        CASE_FROM_TO(ElementType::FLOAT3, VK_FORMAT_R32G32B32_SFLOAT);
        CASE_FROM_TO(ElementType::BYTE4, integer ? VK_FORMAT_R8G8B8A8_SINT : VK_FORMAT_R8G8B8A8_SSCALED);
        CASE_FROM_TO(ElementType::UBYTE4, integer ? VK_FORMAT_R8G8B8A8_UINT : VK_FORMAT_R8G8B8A8_USCALED);
        CASE_FROM_TO(ElementType::SHORT4, integer ? VK_FORMAT_R16G16B16A16_SINT : VK_FORMAT_R16G16B16A16_SSCALED);
        CASE_FROM_TO(ElementType::USHORT4, integer ? VK_FORMAT_R16G16B16A16_UINT : VK_FORMAT_R16G16B16A16_USCALED);
        CASE_FROM_TO(ElementType::HALF4, VK_FORMAT_R16G16B16A16_SFLOAT);
        CASE_FROM_TO(ElementType::FLOAT4, VK_FORMAT_R32G32B32A32_SFLOAT);
    }
    return VK_FORMAT_UNDEFINED;
}

}  // namespace

VulkanVertexBufferInfo::VulkanVertexBufferInfo(uint8_t bufferCount, uint8_t attributeCount, AttributeArray const& attributes)
    : HwVertexBufferInfo(bufferCount, attributeCount), m_info(attributeCount == 0 ? 0 : attributes.size()) {
    // 无属性绘制：顶点属性/绑定描述数量为 0 是 Vulkan 无属性 draw 的前提，无需填充
    EARLY_RETURN(attributeCount == 0);

    auto&                                    soa                 = m_info.m_soa;
    VkVertexInputAttributeDescription* const attribDesc          = soa.Data<PipelineInfo::kAttributeDescription>();
    VkVertexInputBindingDescription* const   bufferDesc          = soa.Data<PipelineInfo::kBufferDescription>();
    VkDeviceSize* const                      offsets             = soa.Data<PipelineInfo::kOffsets>();
    int8_t* const                            attribToBufferIndex = soa.Data<PipelineInfo::kAttributeToBufferIndex>();

    for (uint32_t attribIndex = 0; attribIndex < attributes.size(); attribIndex++) {
        Attribute  attrib       = attributes[attribIndex];
        bool const isInteger    = attrib.flags & Attribute::FLAG_INTEGER;
        bool const isNormalized = attrib.flags & Attribute::FLAG_NORMALIZED;
        VkFormat   vkformat     = GetVkFormat(attrib.type, isNormalized, isInteger);

        // 未绑缓冲的属性复用位置缓冲布局：顶点着色器按 vec4 声明属性，位置元素至少 32bit
        if (attrib.buffer == Attribute::BUFFER_UNUSED) {
            vkformat = isInteger ? VK_FORMAT_R8G8B8A8_UINT : VK_FORMAT_R8G8B8A8_SNORM;
            attrib   = attributes[0];
        }
        offsets[attribIndex] = attrib.offset;
        // location/binding 与 GLSL layout specifier、vkCmdBindVertexBuffers 绑定位置一一对应
        attribDesc[attribIndex] = {
            .location = attribIndex,
            .binding  = attribIndex,
            .format   = vkformat,
        };
        bufferDesc[attribIndex] = {
            .binding = attribIndex,
            .stride  = attrib.stride,
        };
        attribToBufferIndex[attribIndex] = static_cast<int8_t>(attrib.buffer);
        m_attributes.set(attribIndex);
    }
}

VulkanBufferObject::VulkanBufferObject(const VulkanContextPtr& context, VmaAllocator allocator, const VulkanStagePoolPtr& stagePool,
                                       const VulkanBufferCachePtr& bufferCache, uint32_t byteCount, BufferObjectBinding bindingType,
                                       BufferUsage usage)
    : HwBufferObject(byteCount, false),
      bindingType(bindingType),
      m_buffer(context, allocator, stagePool, bufferCache, GetBufferObjectBinding(bindingType), usage, byteCount) {}

void VulkanBufferObject::LoadFromCpu(VulkanCommandBuffer& commands, void const* cpuData, uint32_t byteOffset, uint32_t numBytes) {
    m_buffer.LoadFromCpu(commands, cpuData, byteOffset, numBytes);
}

VulkanIndexBuffer::VulkanIndexBuffer(const VulkanContextPtr& context, VmaAllocator allocator, const VulkanStagePoolPtr& stagePool,
                                     const VulkanBufferCachePtr& bufferCache, uint8_t elementSize, uint32_t indexCount)
    : HwIndexBuffer(elementSize, indexCount, false),
      indexType(elementSize == sizeof(uint16_t) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32),
      m_buffer(context, allocator, stagePool, bufferCache, VulkanBufferBinding::Index, BufferUsage::STATIC,
               static_cast<uint32_t>(elementSize) * indexCount) {}

void VulkanIndexBuffer::LoadFromCpu(VulkanCommandBuffer& commands, void const* cpuData, uint32_t byteOffset, uint32_t numBytes) {
    m_buffer.LoadFromCpu(commands, cpuData, byteOffset, numBytes);
}

// 默认渲染目标：构造期不依赖交换链，附件由 BindSwapChain 注入
VulkanRenderTarget::VulkanRenderTarget() : HwRenderTarget(0, 0), m_offscreen(false), m_protected(false), m_info(std::make_unique<Auxiliary>()) {
    m_info->rpkey.samples = 1;
    m_info->fbkey.samples = 1;
}

VulkanRenderTarget::~VulkanRenderTarget() = default;

void VulkanRenderTarget::BindSwapChain(VulkanSwapChainPtr swapchain) {
    LOG_ASSERT(!m_offscreen);
    LOG_ASSERT(!m_info->colors[0]);

    VkExtent2D const extent = swapchain->GetExtent();
    width                   = extent.width;
    height                  = extent.height;
    m_protected             = swapchain->IsProtected();

    VulkanAttachment color = CreateSwapchainAttachment(swapchain->GetCurrentColor());
    LOG_ASSERT(m_info->attachments.empty());
    m_info->attachments.push_back(color);

    auto& fbkey = m_info->fbkey;
    auto& rpkey = m_info->rpkey;

    rpkey.colorFormat[0] = color.GetFormat();
    rpkey.viewCount      = color.layerCount;
    fbkey.width          = static_cast<uint16_t>(width);
    fbkey.height         = static_cast<uint16_t>(height);
    fbkey.color[0]       = color.GetImageView();
    fbkey.resolve[0]     = VK_NULL_HANDLE;

    if (swapchain->GetDepth()) {
        VulkanAttachment depth = CreateSwapchainAttachment(swapchain->GetDepth());
        m_info->attachments.push_back(depth);
        m_info->depthStencilIndex = 1;

        rpkey.depthStencilFormat = depth.GetFormat();
        fbkey.depthStencil       = depth.GetImageView();
    } else {
        rpkey.depthStencilFormat = VK_FORMAT_UNDEFINED;
        fbkey.depthStencil       = VK_NULL_HANDLE;
    }
    m_info->colors.Set(0);
}

void VulkanRenderTarget::ReleaseSwapchain() {
    m_info->colors = {};
    m_info->attachments.clear();
}

VulkanRenderTarget::VulkanRenderTarget(VkDevice device, VkPhysicalDevice physicalDevice, const VulkanContextPtr& context,
                                       const ResourceManagerPtr& resourceManager, VmaAllocator allocator, VulkanCommands* commands, uint32_t width,
                                       uint32_t height, uint8_t samples, VulkanAttachment color[MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT],
                                       VulkanAttachment depthStencil, const VulkanStagePoolPtr& stagePool, uint8_t layerCount)
    : HwRenderTarget(width, height), m_offscreen(true), m_protected(false), m_info(std::make_unique<Auxiliary>()) {
    // 采样数须同时落在深度与颜色两种 framebuffer 采样掩码内，与 VulkanTexture 的约束一致
    auto const& limits = context->GetPhysicalDeviceLimits();
    samples            = VK_UTILS::ReduceSampleCount(samples, limits.framebufferDepthSampleCounts & limits.framebufferColorSampleCounts);

    auto& rpkey              = m_info->rpkey;
    rpkey.samples            = samples;
    rpkey.depthStencilFormat = depthStencil.GetFormat();
    rpkey.viewCount          = layerCount;

    auto& fbkey   = m_info->fbkey;
    fbkey.width   = static_cast<uint16_t>(width);
    fbkey.height  = static_cast<uint16_t>(height);
    fbkey.samples = samples;

    std::vector<VulkanAttachment>& attachments = m_info->attachments;
    std::vector<VulkanAttachment>  msaaAttachments;

    for (int index = 0; index < MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT; index++) {
        VulkanAttachment& attachment = color[index];
        auto              texture    = attachment.texture;
        if (!texture) {
            rpkey.colorFormat[index] = VK_FORMAT_UNDEFINED;
            continue;
        }

        m_protected |= texture->GetIsProtected();

        size_t const compactIdx = attachments.size();
        attachments.push_back(attachment);
        m_info->colors.Set(index);

        TextureFormat const fmt             = texture->format;
        m_info->colorClearKinds[compactIdx] = IsUnsignedIntFormat(fmt) ? ColorClearKind::UnsignedInt
                                              : IsSignedIntFormat(fmt) ? ColorClearKind::SignedInt
                                                                       : ColorClearKind::Float;

        rpkey.colorFormat[index] = attachment.GetFormat();
        fbkey.color[index]       = attachment.GetImageView();
        fbkey.resolve[index]     = VK_NULL_HANDLE;

        if (samples > 1) {
            VulkanAttachment msaaAttachment = {};
            if (texture->samples == 1) {
                auto msaaTexture = InitMsaaTexture(texture, device, physicalDevice, context, allocator, commands, resourceManager, texture->levels,
                                                   samples, stagePool);
                if (msaaTexture && msaaTexture->IsTransientAttachment()) {
                    rpkey.usesLazilyAllocatedMemory |= static_cast<uint8_t>(1u << index);
                }
                if (attachment.texture->samples == 1) {
                    rpkey.needsResolveMask |= static_cast<uint8_t>(1u << index);
                }
                msaaAttachment = {
                    .texture    = msaaTexture,
                    .layerCount = layerCount,
                };

                fbkey.resolve[index] = attachment.GetImageView();
            } else {
                msaaAttachment = {
                    .texture    = texture,
                    .layerCount = layerCount,
                };
            }
            fbkey.color[index] = msaaAttachment.GetImageView();
            msaaAttachments.push_back(msaaAttachment);
        }
    }

    if (!attachments.empty() && samples > 1 && !msaaAttachments.empty()) {
        m_info->msaaIndex = static_cast<int8_t>(attachments.size());
        attachments.insert(attachments.end(), msaaAttachments.begin(), msaaAttachments.end());
    }

    if (depthStencil.texture) {
        auto depthStencilTexture  = depthStencil.texture;
        m_info->depthStencilIndex = static_cast<int8_t>(attachments.size());
        attachments.push_back(depthStencil);
        fbkey.depthStencil = depthStencil.GetImageView();
        if (samples > 1) {
            m_info->msaaDepthStencilIndex = m_info->depthStencilIndex;
            if (depthStencilTexture->samples == 1) {
                // MSAA 深度纹理的 mip 层级必须为 1
                uint8_t const msLevel = 1;
                // 源深度纹理不可直接作 MSAA 附件，须另建侧车
                auto msaaTexture = InitMsaaTexture(depthStencilTexture, device, physicalDevice, context, allocator, commands, resourceManager,
                                                   msLevel, samples, stagePool);
                m_info->msaaDepthStencilIndex   = static_cast<int8_t>(attachments.size());
                VulkanAttachment msaaAttachment = {
                    .texture    = msaaTexture,
                    .layerCount = layerCount,
                };
                attachments.push_back(msaaAttachment);
                fbkey.depthStencil = msaaAttachment.GetImageView();
            }
        }
    }
}

void VulkanRenderTarget::TransformClientRectToPlatform(VkRect2D* bounds) const {
    auto const& extent = GetExtent();
    ClampToFramebuffer(bounds, extent.width, extent.height);
}

void VulkanRenderTarget::TransformViewportToPlatform(VkViewport* bounds) const { FlipVertically(bounds, GetExtent().height); }

uint8_t VulkanRenderTarget::GetColorTargetCount(VulkanRenderPassContext const& pass) const {
    if (!m_offscreen) {
        return 1;
    }
    if (pass.currentSubpass == 1) {
        return static_cast<uint8_t>(m_info->colors.Count());
    }
    uint8_t count = 0;
    // 只属于第二个子通道的附件在第一个子通道里不参与绘制，故不计入
    m_info->colors.ForEachSetBit([&count, &pass](size_t index) {
        if (!(pass.params.subpassMask & (1u << index))) {
            count++;
        }
    });
    return count;
}

void VulkanRenderTarget::EmitBarriersBeginRenderPass(VulkanCommandBuffer& commands) {
    auto& attachments = m_info->attachments;
    auto  samples     = m_info->fbkey.samples;

    // 布局已是目标布局时无需屏障；若转换路径判定「新旧布局等价而未录制」，仍须补一条
    // 采样器到附件的可写性屏障
    auto barrier = [&commands](VulkanAttachment& attachment, VulkanLayout const layout) {
        auto        tex   = attachment.texture;
        auto const& range = attachment.GetSubresourceRange();
        if (tex->GetLayout(range.baseArrayLayer, range.baseMipLevel) != layout && !tex->TransitionLayout(&commands, range, layout)) {
            tex->SamplerToAttachmentBarrier(&commands, range);
        }
    };

    for (size_t i = 0, count = m_info->colors.Count(); i < count; ++i) {
        auto& attachment = attachments[i];
        auto  tex        = attachment.texture;
        if (samples == 1 || tex->samples == 1) {
            barrier(attachment, VulkanLayout::COLOR_ATTACHMENT);
        }
    }
    if (m_info->msaaIndex != Auxiliary::kUndefinedIndex) {
        for (size_t i = m_info->msaaIndex, count = m_info->msaaIndex + m_info->colors.Count(); i < count; ++i) {
            barrier(attachments[i], VulkanLayout::COLOR_ATTACHMENT);
        }
    }
    if (m_info->depthStencilIndex != Auxiliary::kUndefinedIndex) {
        barrier(attachments[m_info->depthStencilIndex], VulkanLayout::DEPTH_STENCIL_ATTACHMENT);
    }
    if (m_info->msaaDepthStencilIndex != Auxiliary::kUndefinedIndex) {
        barrier(attachments[m_info->msaaDepthStencilIndex], VulkanLayout::DEPTH_STENCIL_ATTACHMENT);
    }
}

void VulkanRenderTarget::EmitBarriersEndRenderPass(VulkanCommandBuffer& commands) {
    // 交换链图像由呈现路径负责布局，不在此转换
    if (IsSwapChain()) {
        return;
    }

    for (auto& attachment : m_info->attachments) {
        auto const& range   = attachment.GetSubresourceRange();
        bool const  isDepth = attachment.IsDepth();
        auto        texture = attachment.texture;
        if (isDepth) {
            // 渲染通道结束时驱动已把布局改成附件的 finalLayout，故先记下再补可采样性屏障
            texture->SetLayout(range, VulkanFboCache::kFinalDepthStencilAttachmentLayout);
            if (!texture->TransitionLayout(&commands, range, VulkanLayout::DEPTH_SAMPLER)) {
                texture->AttachmentToSamplerBarrier(&commands, range);
            }
        } else {
            texture->SetLayout(range, VulkanFboCache::kFinalColorAttachmentLayout);
            if (texture->IsSampleable() && !texture->TransitionLayout(&commands, range, VulkanLayout::FRAG_READ)) {
                texture->AttachmentToSamplerBarrier(&commands, range);
            }
        }
    }
}

VulkanRenderPrimitive::VulkanRenderPrimitive(PrimitiveType primitiveType, VulkanVertexBufferPtr vertexBuffer, VulkanIndexBufferPtr indexBuffer)
    : HwRenderPrimitive{ .type = primitiveType }, vertexBuffer(std::move(vertexBuffer)), indexBuffer(std::move(indexBuffer)) {}

VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(DescriptorSetLayout&& layout, VkDescriptorSetLayout vkLayout)
    : bitmask(Bitmask::FromLayoutDescription(layout)),
      count(Count::FromLayoutBitmask(bitmask)),
      m_vkLayout(vkLayout) {}

VulkanDescriptorSetLayout::Bitmask VulkanDescriptorSetLayout::Bitmask::FromLayoutDescription(DescriptorSetLayout const& layout) {
    Bitmask mask{};
    for (auto const& descriptor : layout.descriptors) {
        switch (descriptor.type) {
            case DescriptorType::UNIFORM_BUFFER: {
                if ((descriptor.flags & DescriptorFlags::DYNAMIC_OFFSET) != DescriptorFlags::NONE) {
                    FromStageFlags(descriptor.stageFlags, descriptor.binding, mask.dynamicUbo);
                } else {
                    FromStageFlags(descriptor.stageFlags, descriptor.binding, mask.ubo);
                }
                break;
            }
            case DescriptorType::SAMPLER_EXTERNAL:
                FromStageFlags(descriptor.stageFlags, descriptor.binding, mask.externalSampler);
                [[fallthrough]];

            case DescriptorType::SAMPLER_2D_FLOAT:
            case DescriptorType::SAMPLER_2D_INT:
            case DescriptorType::SAMPLER_2D_UINT:
            case DescriptorType::SAMPLER_2D_DEPTH:
            case DescriptorType::SAMPLER_2D_ARRAY_FLOAT:
            case DescriptorType::SAMPLER_2D_ARRAY_INT:
            case DescriptorType::SAMPLER_2D_ARRAY_UINT:
            case DescriptorType::SAMPLER_2D_ARRAY_DEPTH:
            case DescriptorType::SAMPLER_CUBE_FLOAT:
            case DescriptorType::SAMPLER_CUBE_INT:
            case DescriptorType::SAMPLER_CUBE_UINT:
            case DescriptorType::SAMPLER_CUBE_DEPTH:
            case DescriptorType::SAMPLER_CUBE_ARRAY_FLOAT:
            case DescriptorType::SAMPLER_CUBE_ARRAY_INT:
            case DescriptorType::SAMPLER_CUBE_ARRAY_UINT:
            case DescriptorType::SAMPLER_CUBE_ARRAY_DEPTH:
            case DescriptorType::SAMPLER_3D_FLOAT:
            case DescriptorType::SAMPLER_3D_INT:
            case DescriptorType::SAMPLER_3D_UINT:
            case DescriptorType::SAMPLER_2D_MS_FLOAT:
            case DescriptorType::SAMPLER_2D_MS_INT:
            case DescriptorType::SAMPLER_2D_MS_UINT:
            case DescriptorType::SAMPLER_2D_MS_ARRAY_FLOAT:
            case DescriptorType::SAMPLER_2D_MS_ARRAY_INT:
            case DescriptorType::SAMPLER_2D_MS_ARRAY_UINT: {
                FromStageFlags(descriptor.stageFlags, descriptor.binding, mask.sampler);
                break;
            }
            case DescriptorType::INPUT_ATTACHMENT: {
                FromStageFlags(descriptor.stageFlags, descriptor.binding, mask.inputAttachment);
                break;
            }
            case DescriptorType::SHADER_STORAGE_BUFFER:
                LOG_CRITICAL("Shader storage is not supported");
                break;
        }
    }
    return mask;
}

VulkanDescriptorSet::VulkanDescriptorSet(VulkanDescriptorSetLayoutPtr layout, OnRecycle&& onRecycleFn, VkDescriptorSet vkSet)
    : boundLayout(layout->GetVkLayout()),
      dynamicUboMask(layout->bitmask.dynamicUbo),
      uniqueDynamicUboCount(layout->count.dynamicUbo),
      m_layout(std::move(layout)),
      m_currentSetIndex(0) {
    AddNewSet(vkSet, std::move(onRecycleFn));
}

VulkanDescriptorSet::~VulkanDescriptorSet() {
    for (auto const& setBundle : m_sets) {
        if (setBundle.onRecycleFn) {
            setBundle.onRecycleFn(this);
        }
    }
}

void VulkanDescriptorSet::ReferencedBy(VulkanCommandBuffer& commands) {
    m_uboMask.ForEachSetBit([this, &commands](size_t index) {
        auto* bufferObject = static_cast<VulkanBufferObject*>(m_resources[index].Get());
        bufferObject->ReferencedBy(commands);
    });
    m_sets[m_currentSetIndex].fenceStatus = commands.GetFenceStatus();
}

void VulkanDescriptorSet::AddNewSet(VkDescriptorSet vkSet, OnRecycle&& onRecycleFn) {
    Gc();
    m_currentSetIndex = static_cast<uint8_t>(m_sets.size());
    m_sets.push_back({ vkSet, std::move(onRecycleFn), {} });
}

void VulkanDescriptorSet::Gc() {
    size_t empty = 0;
    for (auto& setBundle : m_sets) {
        LOG_ASSERT(setBundle.onRecycleFn);
        if (setBundle.vkSet == VK_NULL_HANDLE) {
            empty++;
            continue;
        }
        if (setBundle.fenceStatus && setBundle.fenceStatus->GetStatus() == VK_SUCCESS) {
            setBundle.onRecycleFn(this);
            setBundle.vkSet        = VK_NULL_HANDLE;
            setBundle.fenceStatus  = {};
            empty++;
        }
    }

    // 空槽积累到一定量才压缩，避免每次绑定都搬一次数组
    if (empty > kDescriptorSetGcLimit) {
        std::vector<InternalVkSet> retainedSets;
        for (auto& setBundle : m_sets) {
            if (setBundle.vkSet != VK_NULL_HANDLE) {
                retainedSets.push_back({ setBundle.vkSet, std::move(setBundle.onRecycleFn), std::move(setBundle.fenceStatus) });
            }
        }
        std::swap(m_sets, retainedSets);
    }
}

END_NS_BACKEND
