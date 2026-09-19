#include "vulkan/VulkanTexture.h"

#include "Utils/Debug.h"
#include "Utils/Log.h"
#include "Utils/Panic.h"

#include <algorithm>
#include <cstring>

#include "Backend/PixelBufferDescriptor.h"
#include "vulkan/VkDef.h"
#include "vulkan/commands/VulkanCommandBuffer.h"
#include "vulkan/commands/VulkanCommands.h"
#include "vulkan/utils/Conversion.h"

BEGIN_NS_BACKEND

namespace {

uint8_t getLayerCount(SamplerType const target, uint32_t const depth) {
    switch (target) {
        case SamplerType::SAMPLER_2D:
        case SamplerType::SAMPLER_3D:
        case SamplerType::SAMPLER_EXTERNAL:
            return 1;
        case SamplerType::SAMPLER_CUBEMAP:
            return 6;
        case SamplerType::SAMPLER_CUBEMAP_ARRAY:
            return depth * 6;
        case SamplerType::SAMPLER_2D_ARRAY:
            return depth;
    }
    return 1;
}

VkComponentMapping composeSwizzle(VkComponentMapping const& prev, VkComponentMapping const& next) {
    static constexpr VkComponentSwizzle kIdentity[] = {
        VK_COMPONENT_SWIZZLE_R,
        VK_COMPONENT_SWIZZLE_G,
        VK_COMPONENT_SWIZZLE_B,
        VK_COMPONENT_SWIZZLE_A,
    };

    auto const compose = [](VkComponentMapping const& lhs, VkComponentMapping const& rhs) -> VkComponentMapping {
        VkComponentSwizzle vals[4] = { rhs.r, rhs.g, rhs.b, rhs.a };
        for (auto& out : vals) {
            switch (out) {
                case VK_COMPONENT_SWIZZLE_R:
                    out = lhs.r;
                    break;
                case VK_COMPONENT_SWIZZLE_G:
                    out = lhs.g;
                    break;
                case VK_COMPONENT_SWIZZLE_B:
                    out = lhs.b;
                    break;
                case VK_COMPONENT_SWIZZLE_A:
                    out = lhs.a;
                    break;
                // 以下取值原样保留
                case VK_COMPONENT_SWIZZLE_IDENTITY:
                case VK_COMPONENT_SWIZZLE_ZERO:
                case VK_COMPONENT_SWIZZLE_ONE:
                case VK_COMPONENT_SWIZZLE_MAX_ENUM:
                    break;
            }
        }
        return { vals[0], vals[1], vals[2], vals[3] };
    };

    // IDENTITY 与「取同名字段」等价，但合成时必须显式化，否则 IDENTITY 会吞掉前一次的映射
    auto const identityToChannel = [](VkComponentMapping const& mapping) -> VkComponentMapping {
        VkComponentSwizzle vals[4] = { mapping.r, mapping.g, mapping.b, mapping.a };
        for (uint8_t i = 0; i < 4; i++) {
            if (vals[i] == VK_COMPONENT_SWIZZLE_IDENTITY) {
                vals[i] = kIdentity[i];
            }
        }
        return { vals[0], vals[1], vals[2], vals[3] };
    };
    auto const channelToIdentity = [](VkComponentMapping const& mapping) -> VkComponentMapping {
        VkComponentSwizzle vals[4] = { mapping.r, mapping.g, mapping.b, mapping.a };
        for (uint8_t i = 0; i < 4; i++) {
            if (kIdentity[i] == vals[i]) {
                vals[i] = VK_COMPONENT_SWIZZLE_IDENTITY;
            }
        }
        return { vals[0], vals[1], vals[2], vals[3] };
    };

    VkComponentMapping const prevExplicit = identityToChannel(prev);
    VkComponentMapping const nextExplicit = identityToChannel(next);
    return channelToIdentity(compose(prevExplicit, nextExplicit));
}

VulkanLayout getDefaultLayoutImpl(TextureUsage usage) {
    if (HasAnyFlag(usage, TextureUsage::DEPTH_ATTACHMENT)) {
        return HasAnyFlag(usage, TextureUsage::SAMPLEABLE) ? VulkanLayout::DEPTH_SAMPLER : VulkanLayout::DEPTH_STENCIL_ATTACHMENT;
    }

    if (HasAnyFlag(usage, TextureUsage::COLOR_ATTACHMENT)) {
        return HasAnyFlag(usage, TextureUsage::SAMPLEABLE) ? VulkanLayout::FRAG_READ : VulkanLayout::COLOR_ATTACHMENT;
    }

    // 不可写纹理的默认布局即最优只读布局
    return VulkanLayout::FRAG_READ;
}

VulkanLayout getDefaultLayoutImpl(VkImageUsageFlags vkusage) {
    TextureUsage usage{};
    if (vkusage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        usage = usage | TextureUsage::DEPTH_ATTACHMENT;
    }
    if (vkusage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
        usage = usage | TextureUsage::COLOR_ATTACHMENT;
    }
    if (vkusage & VK_IMAGE_USAGE_SAMPLED_BIT) {
        usage = usage | TextureUsage::SAMPLEABLE;
    }
    return getDefaultLayoutImpl(usage);
}

SamplerType getSamplerTypeFromDepth(uint32_t const depth) {
    return depth > 1 ? SamplerType::SAMPLER_2D_ARRAY : SamplerType::SAMPLER_2D;
}

uint8_t getLayerCountFromDepth(uint32_t const depth) { return getLayerCount(getSamplerTypeFromDepth(depth), depth); }

VkImageUsageFlags getUsage(const VulkanContextPtr& context, uint8_t samples, VkPhysicalDevice physicalDevice, VkFormat vkFormat,
                           TextureUsage tusage) {
    VkImageUsageFlags usage = {};
    if (HasAnyFlag(tusage, TextureUsage::BLIT_SRC)) {
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if (HasAnyFlag(tusage, TextureUsage::BLIT_DST)) {
        usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    if (HasAnyFlag(tusage, TextureUsage::GEN_MIPMAPPABLE)) {
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    constexpr TextureUsage kDepthStencilUsage = TextureUsage::DEPTH_ATTACHMENT | TextureUsage::STENCIL_ATTACHMENT;

    // 惰性分配内存只有在「用途全是附件标志」时才省显存；深度解析走自定义着色器，不能算作纯附件
    bool const useTransientAttachment = context->IsLazilyAllocatedMemorySupported() &&
                                        !HasAnyFlag(tusage, ~TextureUsage::ALL_ATTACHMENTS) &&
                                        HasAnyFlag(tusage, TextureUsage::ALL_ATTACHMENTS) &&
                                        (!HasAnyFlag(tusage, kDepthStencilUsage) || samples == 1);

    VkImageUsageFlags const transientFlag = useTransientAttachment ? VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT : 0U;

    if (HasAnyFlag(tusage, TextureUsage::SAMPLEABLE)) {
#if BVK_ENABLED(BVK_DEBUG_TEXTURE)
        if (physicalDevice != VK_NULL_HANDLE) {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(physicalDevice, vkFormat, &props);
            if (!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
                LOG_WARN("Texture usage is SAMPLEABLE but format {} is not sampleable with optimal tiling.", static_cast<int>(vkFormat));
            }
        }
#endif
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (HasAnyFlag(tusage, TextureUsage::COLOR_ATTACHMENT)) {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | transientFlag;
        if (HasAnyFlag(tusage, TextureUsage::SUBPASS_INPUT)) {
            usage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
        }
    }
    if (HasAnyFlag(tusage, TextureUsage::STENCIL_ATTACHMENT)) {
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | transientFlag;
    }
    if (HasAnyFlag(tusage, TextureUsage::UPLOADABLE)) {
        usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    if (HasAnyFlag(tusage, TextureUsage::DEPTH_ATTACHMENT)) {
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | transientFlag;

        // 深度解析走自定义着色器，因此 MSAA 深度图必须可采样
        if (samples > 1) {
            usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        }
    }
    return usage;
}

void adjustedMemcpy(void* mapped, PixelBufferDescriptor const& p, size_t width, size_t height, size_t depth) {
    auto const* buf      = static_cast<uint8_t const*>(p.buffer);
    size_t const pixelSize = PixelBufferDescriptor::ComputePixelSize(p.format, p.type);
    size_t const pbdStride = p.stride ? p.stride : width;

    assert_invariant(pbdStride >= width);

    // 只有存在行间填充或非零偏移时才需要逐行搬运
    if (pixelSize > 0 && (p.left > 0 || p.top > 0 || pbdStride > width)) {
        size_t const pbdRowSize   = PixelBufferDescriptor::ComputeDataSize(p.format, p.type, pbdStride, 1, p.alignment);
        size_t const pbdHeight    = p.size / pixelSize / pbdStride / depth;
        size_t const pbdLayerSize = pbdRowSize * pbdHeight;

        size_t const rowSize   = width * pixelSize;
        size_t const layerSize = width * height * pixelSize;

        size_t const writeSize = std::min(pbdStride - p.left, width) * pixelSize;

        for (size_t z = 0; z < depth; z++) {
            for (size_t y = p.top; y < pbdHeight; y++) {
                uint8_t const* src = buf + ((p.left * pixelSize) + (y * pbdRowSize) + (z * pbdLayerSize));
                auto*          dst = static_cast<uint8_t*>(mapped) + ((y - p.top) * rowSize + z * layerSize);
                memcpy(dst, src, writeSize);
            }
        }
        return;
    }

    // pixelSize 为 0 表示压缩格式，其布局由格式自身描述，按缓冲长度整体拷贝
    size_t const writeSize = pixelSize > 0 ? pixelSize * (width * height * depth) : p.size;
    memcpy(mapped, buf, writeSize);
}

uint8_t getAlignmentForBufferToImageCopy(VkFormat format) {
    // VUID-vkCmdCopyBufferToImage-dstImage-07978
    if (VK_UTILS::IsVkDepthFormat(format) || VK_UTILS::IsVkStencilFormat(format)) {
        return 4;
    }

    if (VK_UTILS::IsVkYcbcrConversionFormat(format)) {
        assert_invariant(false && "Multi planar format is not supported");
        return 1;
    }

    // VUID-vkCmdCopyBufferToImage-dstImage-07975
    return VK_UTILS::GetTexelBlockSize(format);
}

}  // namespace

VulkanTextureState::VulkanTextureState(const VulkanStagePoolPtr& stagePool, const VulkanCommandsPtr& commands, VmaAllocator allocator,
                                       VkDevice device, VkImage image, VkDeviceMemory deviceMemory, VkFormat format,
                                       VkImageViewType viewType, uint8_t levels, uint8_t layerCount,
                                       VkSamplerYcbcrConversion ycbcrConversion, VkImageUsageFlags usage, bool isProtected)
    : m_stagePool(stagePool),
      m_commands(commands),
      m_allocator(allocator),
      m_device(device),
      m_textureImage(image),
      m_textureImageMemory(deviceMemory),
      m_vkFormat(format),
      m_viewType(viewType),
      m_fullViewRange{ VK_UTILS::GetImageAspect(format), 0, levels, 0, layerCount },
      m_ycbcr{ ycbcrConversion },
      m_defaultLayout(getDefaultLayoutImpl(usage)),
      m_usage(usage),
      m_isProtected(isProtected) {}

VulkanTextureState::~VulkanTextureState() {
    clearCachedImageViews();
    // 内存句柄为空表示图像由外部创建，本对象只借用
    if (m_textureImageMemory != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_textureImage, kVkAlloc);
        vkFreeMemory(m_device, m_textureImageMemory, kVkAlloc);
    }
}

void VulkanTextureState::clearCachedImageViews() noexcept {
    for (auto entry : m_cachedImageViews) {
        vkDestroyImageView(m_device, entry.second, kVkAlloc);
    }
    m_cachedImageViews.clear();
}

VkImageView VulkanTextureState::getImageView(VkImageSubresourceRange range, VkImageViewType viewType, VkComponentMapping swizzle) {
    ImageViewKey const key{ range, viewType, swizzle };
    if (auto iter = m_cachedImageViews.find(key); iter != m_cachedImageViews.end()) {
        return iter->second;
    }

    VkSamplerYcbcrConversionInfo conversionInfo = {
        .sType      = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO,
        .conversion = m_ycbcr.conversion,
    };

    // 带外部转换时格式必须留空，由转换对象决定实际格式
    VkImageViewCreateInfo const viewInfo = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext            = m_ycbcr.conversion != VK_NULL_HANDLE ? &conversionInfo : nullptr,
        .flags            = 0,
        .image            = m_textureImage,
        .viewType         = viewType,
        .format           = m_ycbcr.conversion != VK_NULL_HANDLE ? VK_FORMAT_UNDEFINED : m_vkFormat,
        .components       = swizzle,
        .subresourceRange = range,
    };

    VkImageView imageView = VK_NULL_HANDLE;
    vkCreateImageView(m_device, &viewInfo, kVkAlloc, &imageView);
    m_cachedImageViews.emplace(key, imageView);
    return imageView;
}

// 包装外部已创建的 VkImage（交换链图像、外部图像）；deviceMemory 为空时图像不归本对象所有
VulkanTexture::VulkanTexture(const VulkanContextPtr& context, VkDevice device, VmaAllocator allocator,
                             const ResourceManagerPtr& resourceManager, const VulkanCommandsPtr& commands, VkImage image,
                             VkDeviceMemory deviceMemory, VkFormat format, VkSamplerYcbcrConversion conversion, uint8_t samples,
                             uint32_t width, uint32_t height, uint32_t depth, TextureUsage tusage,
                             const VulkanStagePoolPtr& stagePool)
    : HwTexture(getSamplerTypeFromDepth(depth), 1, samples, width, height, depth, TextureFormat::UNUSED, tusage, false),
      m_state(resourceManager->AllocateAndConstruct<VulkanTextureState>(
              stagePool, commands, allocator, device, image, deviceMemory, format, VK_UTILS::GetViewType(SamplerType::SAMPLER_2D),
              /*levels=*/1, getLayerCountFromDepth(depth), conversion, getUsage(context, samples, VK_NULL_HANDLE, format, tusage),
              HasAnyFlag(tusage, TextureUsage::PROTECTED))) {
    m_primaryViewRange = m_state->m_fullViewRange;
}

// 从零创建：驱动 createTextureR 的正路
VulkanTexture::VulkanTexture(VkDevice device, VkPhysicalDevice physicalDevice, const VulkanContextPtr& context,
                             VmaAllocator allocator, const ResourceManagerPtr& resourceManager, const VulkanCommandsPtr& commands,
                             SamplerType target, uint8_t levels, TextureFormat tformat, uint8_t samples, uint32_t w, uint32_t h,
                             uint32_t depth, TextureUsage tusage, const VulkanStagePoolPtr& stagePool)
    : HwTexture(target, levels, samples, w, h, depth, tformat, tusage, false) {
    VkFormat const vkFormat    = VK_UTILS::GetVkFormat(tformat);
    bool const     isProtected = HasAnyFlag(tusage, TextureUsage::PROTECTED);

    VkImageCreateInfo imageInfo{
        .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags       = isProtected ? VK_IMAGE_CREATE_PROTECTED_BIT : 0u,
        .imageType   = target == SamplerType::SAMPLER_3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D,
        .format      = vkFormat,
        .extent      = { w, h, depth },
        .mipLevels   = levels,
        .arrayLayers = 1,
        .tiling      = VK_IMAGE_TILING_OPTIMAL,
        .usage       = getUsage(context, samples, physicalDevice, vkFormat, tusage),
    };

    if (target == SamplerType::SAMPLER_3D && HasAnyFlag(tusage, TextureUsage::ALL_ATTACHMENTS)) {
        if (context->IsImageView2DOn3DImageSupported()) {
            // 该标志只为「3D 图像作渲染目标」而设，不足以让 2D 视图参与采样
            imageInfo.flags = VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
        } else {
            LOG_WARN("Note: creating 2D views on 3D image is not available on this platform. "
                     "i.e. we cannot render to slices of a 3D image");
        }
    } else if (target == SamplerType::SAMPLER_CUBEMAP) {
        imageInfo.arrayLayers = 6;
        imageInfo.flags       = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    } else if (target == SamplerType::SAMPLER_2D_ARRAY) {
        imageInfo.arrayLayers  = depth;
        imageInfo.extent.depth = 1;
        // 不用 VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT：MoltenVK 不支持，且它只对 3D 图像的
        // 数组式访问有意义——「数组性」属于 VkImageView 而非 VkImage
    }
    if (target == SamplerType::SAMPLER_CUBEMAP_ARRAY) {
        imageInfo.arrayLayers  = depth * 6;
        imageInfo.extent.depth = 1;
    }
    if (isProtected) {
        imageInfo.flags |= VK_IMAGE_CREATE_PROTECTED_BIT;
    }

    // 采样数与作为附件时的尺寸上限都取设备能力的交集，否则创建出的图像无法被使用
    auto const& limits = context->GetPhysicalDeviceLimits();
    if (imageInfo.usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
        samples = VK_UTILS::ReduceSampleCount(samples, VK_UTILS::IsVkDepthFormat(vkFormat) ? limits.sampledImageDepthSampleCounts
                                                                                          : limits.sampledImageColorSampleCounts);
    }
    if (imageInfo.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
        samples = VK_UTILS::ReduceSampleCount(samples, limits.framebufferColorSampleCounts);
    }
    if (imageInfo.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        samples = VK_UTILS::ReduceSampleCount(samples, limits.sampledImageDepthSampleCounts);
    }
    this->samples     = samples;
    imageInfo.samples = static_cast<VkSampleCountFlagBits>(samples);

    VkImage        textureImage = VK_NULL_HANDLE;
    VkResult const createResult = vkCreateImage(device, &imageInfo, kVkAlloc, &textureImage);
    if (createResult != VK_SUCCESS || BVK_ENABLED(BVK_DEBUG_TEXTURE)) {
        LOG_DEBUG(
                "vkCreateImage: image = {}, result = {}, extent = {}x{}x{}, mipLevels = {}, usage = {}, samples = {}, type = {}, "
                "flags = {}, target = {}, format = {}",
                static_cast<void*>(textureImage), static_cast<int>(createResult), w, h, depth, static_cast<int>(levels),
                static_cast<int>(imageInfo.usage), static_cast<int>(imageInfo.samples), static_cast<int>(imageInfo.imageType),
                static_cast<int>(imageInfo.flags), static_cast<int>(target), static_cast<int>(vkFormat));
    }
    FILAMENT_CHECK_POSTCONDITION(createResult == VK_SUCCESS)
            << "Unable to create image." << " error=" << static_cast<int32_t>(createResult);

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, textureImage, &memReqs);

    bool const    useTransientAttachment = (imageInfo.usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) != 0;
    VkFlags const requiredMemoryFlags    = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                           (useTransientAttachment ? VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT : 0U) |
                                           (isProtected ? VK_MEMORY_PROPERTY_PROTECTED_BIT : 0U);
    uint32_t const memoryTypeIndex = context->SelectMemoryType(memReqs.memoryTypeBits, requiredMemoryFlags);
    FILAMENT_CHECK_POSTCONDITION(memoryTypeIndex < VK_MAX_MEMORY_TYPES)
            << "VulkanTexture: unable to find a memory type that meets requirements.";

    VkMemoryAllocateInfo const allocInfo{
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = memReqs.size,
        .memoryTypeIndex = memoryTypeIndex,
    };

    VkDeviceMemory textureImageMemory = VK_NULL_HANDLE;
    VkResult       result             = vkAllocateMemory(device, &allocInfo, kVkAlloc, &textureImageMemory);
    FILAMENT_CHECK_POSTCONDITION(result == VK_SUCCESS)
            << "Unable to allocate image memory." << " error=" << static_cast<int32_t>(result);
    result = vkBindImageMemory(device, textureImage, textureImageMemory, 0);
    FILAMENT_CHECK_POSTCONDITION(result == VK_SUCCESS) << "Unable to bind image." << " error=" << static_cast<int32_t>(result);

    m_state = resourceManager->AllocateAndConstruct<VulkanTextureState>(
            stagePool, commands, allocator, device, textureImage, textureImageMemory, vkFormat, VK_UTILS::GetViewType(target), levels,
            getLayerCount(target, depth), VK_NULL_HANDLE /* ycbcrConversion */, imageInfo.usage, isProtected);

    m_primaryViewRange = m_state->m_fullViewRange;
}

VulkanTexture::VulkanTexture([[maybe_unused]] VkDevice device, [[maybe_unused]] VkPhysicalDevice physicalDevice,
                             [[maybe_unused]] const VulkanContextPtr& context, [[maybe_unused]] VmaAllocator allocator,
                             [[maybe_unused]] const VulkanCommandsPtr& commands, const VulkanTexturePtr& src, uint8_t baseLevel,
                             uint8_t levelCount)
    : HwTexture(src->target, src->levels, src->samples, src->width, src->height, src->depth, src->format, src->usage, src->asynchronous),
      m_state(src->m_state) {
    m_primaryViewRange              = src->m_primaryViewRange;
    m_primaryViewRange.baseMipLevel = src->m_primaryViewRange.baseMipLevel + baseLevel;
    m_primaryViewRange.levelCount   = levelCount;
}

VulkanTexture::VulkanTexture([[maybe_unused]] VkDevice device, [[maybe_unused]] VkPhysicalDevice physicalDevice,
                             [[maybe_unused]] const VulkanContextPtr& context, [[maybe_unused]] VmaAllocator allocator,
                             [[maybe_unused]] const VulkanCommandsPtr& commands, const VulkanTexturePtr& src, VkComponentMapping swizzle)
    : HwTexture(src->target, src->levels, src->samples, src->width, src->height, src->depth, src->format, src->usage, src->asynchronous),
      m_state(src->m_state),
      m_primaryViewRange(src->m_primaryViewRange),
      m_swizzle(composeSwizzle(src->m_swizzle, swizzle)) {}

void VulkanTexture::UpdateImage(const PixelBufferDescriptor& data, uint32_t width, uint32_t height, uint32_t depth, uint32_t xoffset,
                                uint32_t yoffset, uint32_t zoffset, uint32_t miplevel) {
    assert_invariant(width <= this->width && height <= this->height);
    assert_invariant(depth <= this->depth * ((target == SamplerType::SAMPLER_CUBEMAP ||
                                              target == SamplerType::SAMPLER_CUBEMAP_ARRAY) ? 6 : 1));
    assert_invariant(!m_state->m_isProtected);

    // 上游此处还会经 DataReshaper 把 3 分量数据补成 4 分量；该前端件不在移植范围内，按原样上传
    VkFormat const hostFormat   = VK_UTILS::GetVkFormat(data.format, data.type);
    VkFormat const deviceFormat = VK_UTILS::GetVkFormatLinear(m_state->m_vkFormat);
    if (hostFormat != deviceFormat && hostFormat != VK_FORMAT_UNDEFINED) {
        assert_invariant(xoffset == 0 && yoffset == 0 && zoffset == 0 && "Offsets not yet supported when format conversion is required.");
        updateImageWithBlit(data, width, height, depth, miplevel);
        return;
    }

    assert_invariant(data.size > 0 && "Data is empty");

    size_t const bpp = PixelBufferDescriptor::ComputePixelSize(data.format, data.type);
    // bpp 为 0 表示压缩格式，其大小由缓冲长度决定
    size_t const writeSize = bpp > 0 ? size_t(width) * height * depth * bpp : data.size;

    FILAMENT_CHECK_PRECONDITION(m_state->m_commands != nullptr);

    // segment 必须在命令缓冲提交前一直被登记，暂存池才能正确追踪其占用
    uint8_t const                       alignment    = getAlignmentForBufferToImageCopy(m_state->m_vkFormat);
    VulkanStageBuffer::SegmentPtr const stageSegment = m_state->m_stagePool->AcquireStage(writeSize, alignment);
    assert_invariant(stageSegment->GetMemory() != VK_NULL_HANDLE);

    adjustedMemcpy(stageSegment->GetMapping(), data, width, height, depth);
    vmaFlushAllocation(m_state->m_allocator, stageSegment->GetMemory(), stageSegment->GetOffset(), writeSize);

    VulkanCommandBuffer&  commands = m_state->m_commands->Get();
    VkCommandBuffer const cmdbuf   = commands.Buffer();
    commands.Acquire(stageSegment);
    commands.Acquire(VulkanTexturePtr(this));

    bool const isDepth = (GetImageAspect() & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;

    VkBufferImageCopy copyRegion = {
        .bufferOffset      = stageSegment->GetOffset(),
        .bufferRowLength   = {},
        .bufferImageHeight = {},
        .imageSubresource  = {
            .aspectMask     = VkImageAspectFlags(isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT),
            .mipLevel       = miplevel,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
        .imageOffset = { int32_t(xoffset), int32_t(yoffset), int32_t(zoffset) },
        .imageExtent = { width, height, depth },
    };

    VkImageSubresourceRange transitionRange = {
        .aspectMask     = GetImageAspect(),
        .baseMipLevel   = miplevel,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };

    // 3D 纹理按 z 方向切片，2D 数组按层：两者的子资源划分方式不同
    if (target == SamplerType::SAMPLER_2D_ARRAY || target == SamplerType::SAMPLER_CUBEMAP ||
        target == SamplerType::SAMPLER_CUBEMAP_ARRAY) {
        copyRegion.imageOffset.z                   = 0;
        copyRegion.imageExtent.depth               = 1;
        copyRegion.imageSubresource.baseArrayLayer = zoffset;
        copyRegion.imageSubresource.layerCount     = depth;
        transitionRange.baseArrayLayer             = zoffset;
        transitionRange.layerCount                 = depth;
    }

    constexpr VulkanLayout kNewLayout = VulkanLayout::TRANSFER_DST;
    VulkanLayout           nextLayout = GetLayout(transitionRange.baseArrayLayer, miplevel);
    VkImageLayout const    newVkLayout = VK_UTILS::GetVkLayout(kNewLayout);

    // 首次上传时按用途推断上传后应回到的布局
    if (nextLayout == VulkanLayout::UNDEFINED) {
        nextLayout = GetDefaultLayout();
    }

    TransitionLayout(&commands, transitionRange, kNewLayout);

    vkCmdCopyBufferToImage(cmdbuf, stageSegment->GetVkBuffer(), m_state->m_textureImage, newVkLayout, 1, &copyRegion);

    TransitionLayout(&commands, transitionRange, nextLayout);
}

void VulkanTexture::updateImageWithBlit(const PixelBufferDescriptor& data, uint32_t width, uint32_t height, uint32_t depth,
                                        uint32_t miplevel) {
    size_t const bpp = PixelBufferDescriptor::ComputePixelSize(data.format, data.type);
    // bpp 为 0 表示压缩格式，其大小由缓冲长度决定
    size_t const writeSize = bpp > 0 ? size_t(width) * height * depth * bpp : data.size;

    FILAMENT_CHECK_PRECONDITION(m_state->m_commands != nullptr);

    void*                          mapped = nullptr;
    VulkanStageImage::ResourcePtr const stage = m_state->m_stagePool->AcquireStageImage(data.format, data.type, width, height);
    vmaMapMemory(m_state->m_allocator, stage->GetMemory(), &mapped);
    adjustedMemcpy(mapped, data, width, height, depth);
    vmaUnmapMemory(m_state->m_allocator, stage->GetMemory());
    vmaFlushAllocation(m_state->m_allocator, stage->GetMemory(), 0, writeSize);

    VulkanCommandBuffer&  commands = m_state->m_commands->Get();
    VkCommandBuffer const cmdbuf   = commands.Buffer();
    commands.Acquire(stage);
    commands.Acquire(VulkanTexturePtr(this));

    // blit 形式的格式转换不支持 3D 图像与 cubemap，只处理第 0 层
    constexpr uint32_t kLayer = 0;

    VkOffset3D const         rect[2]{ { 0, 0, 0 }, { int32_t(width), int32_t(height), 1 } };
    VkImageAspectFlags const aspect = GetImageAspect();

    VkImageBlit const blitRegions[1] = { {
            .srcSubresource = { aspect, 0, 0, 1 },
            .srcOffsets     = { rect[0], rect[1] },
            .dstSubresource = { aspect, miplevel, kLayer, 1 },
            .dstOffsets     = { rect[0], rect[1] },
    } };

    VkImageSubresourceRange const range = { aspect, miplevel, 1, kLayer, 1 };

    constexpr VulkanLayout kNewLayout = VulkanLayout::TRANSFER_DST;
    VulkanLayout const     oldLayout  = GetLayout(kLayer, miplevel);
    TransitionLayout(&commands, range, kNewLayout);

    vkCmdBlitImage(cmdbuf, stage->GetImage(), VK_UTILS::GetVkLayout(VulkanLayout::TRANSFER_SRC), m_state->m_textureImage,
                   VK_UTILS::GetVkLayout(kNewLayout), 1, blitRegions, VK_FILTER_NEAREST);

    TransitionLayout(&commands, range, oldLayout);
}

VulkanLayout VulkanTexture::GetSamplerLayout() const {
    if (!IsSampleable()) {
        return VulkanLayout::UNDEFINED;
    }
    if (m_state->m_usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        return VulkanLayout::DEPTH_SAMPLER;
    }
    return VulkanLayout::FRAG_READ;
}

VkImageView VulkanTexture::GetAttachmentView(const VkImageSubresourceRange& range, VkImageViewType type) {
    assert_invariant(m_state->m_ycbcr.conversion == VK_NULL_HANDLE &&
                     "We are not yet supporting external image as attachments.");
    // 2D 附件视图只覆盖单个切片，多层的场景由 2D_ARRAY 承载
    if (type == VK_IMAGE_VIEW_TYPE_2D) {
        VkImageSubresourceRange copy = range;
        copy.levelCount              = 1;
        copy.layerCount              = 1;
        return getImageView(copy, type, {});
    }
    return getImageView(range, type, {});
}

VkImageView VulkanTexture::GetView(const VkImageSubresourceRange& range) {
    return getImageView(range, m_state->m_viewType, m_swizzle);
}

VkImageAspectFlags VulkanTexture::GetImageAspect() const { return VK_UTILS::GetImageAspect(m_state->m_vkFormat); }

VkImageView VulkanTexture::getImageView(VkImageSubresourceRange range, VkImageViewType viewType, VkComponentMapping swizzle) {
    return m_state->getImageView(range, viewType, swizzle);
}

bool VulkanTexture::TransitionLayout(VulkanCommandBuffer* commands, const VkImageSubresourceRange& range, VulkanLayout newLayout) {
    if (TransitionLayout(commands->Buffer(), range, newLayout)) {
        commands->Acquire(VulkanTexturePtr(this));
        return true;
    }
    return false;
}

bool VulkanTexture::TransitionLayout(VkCommandBuffer cmdbuf, const VkImageSubresourceRange& range, VulkanLayout newLayout) {
    VulkanLayout const oldLayout = GetLayout(range.baseArrayLayer, range.baseMipLevel);

    uint32_t const firstLayer  = range.baseArrayLayer;
    uint32_t const lastLayer   = firstLayer + range.layerCount;
    uint32_t const firstLevel  = range.baseMipLevel;
    uint32_t const lastLevel   = firstLevel + range.levelCount;

    // 范围跨多个 layer/level 时，只有当它们布局一致才能一次转换；否则校验层会因 oldLayout 不符而报错
    bool transitionSliceBySlice = false;
    for (uint32_t i = firstLayer; i < lastLayer; ++i) {
        for (uint32_t j = firstLevel; j < lastLevel; ++j) {
            if (oldLayout != GetLayout(i, j)) {
                transitionSliceBySlice = true;
                break;
            }
        }
    }

    bool hasTransitions = false;
    if (transitionSliceBySlice) {
        for (uint32_t i = firstLayer; i < lastLayer; ++i) {
            for (uint32_t j = firstLevel; j < lastLevel; ++j) {
                VulkanLayout const layout = GetLayout(i, j);
                if (layout == newLayout) {
                    continue;
                }
                hasTransitions = hasTransitions || VK_UTILS::TransitionLayout(cmdbuf, {
                                                                                     .image      = m_state->m_textureImage,
                                                                                     .oldLayout  = layout,
                                                                                     .newLayout  = newLayout,
                                                                                     .subresources = {
                                                                                             .aspectMask     = range.aspectMask,
                                                                                             .baseMipLevel   = j,
                                                                                             .levelCount     = 1,
                                                                                             .baseArrayLayer = i,
                                                                                             .layerCount     = 1,
                                                                                     },
                                                                             });
            }
        }
    } else if (newLayout != oldLayout) {
        hasTransitions = VK_UTILS::TransitionLayout(cmdbuf, {
                                                                .image      = m_state->m_textureImage,
                                                                .oldLayout  = oldLayout,
                                                                .newLayout  = newLayout,
                                                                .subresources = range,
                                                        });
    }

    // 即使没有真正发出屏障，本次调用后也应按新布局记账
    SetLayout(range, newLayout);

#if BVK_ENABLED(BVK_DEBUG_LAYOUT_TRANSITION)
    LOG_DEBUG("transition texture={} ({},{}) count=({},{}) old={} new={} hasTransitions={} sliceBySlice={}",
              static_cast<void*>(m_state->m_textureImage), range.baseArrayLayer, range.baseMipLevel, range.layerCount,
              range.levelCount, oldLayout, newLayout, hasTransitions, transitionSliceBySlice);
#endif

    return hasTransitions;
}

void VulkanTexture::SamplerToAttachmentBarrier(VulkanCommandBuffer* commands, const VkImageSubresourceRange& range) {
    VkCommandBuffer const cmdbuf = commands->Buffer();
    VkImageLayout const   layout = VK_UTILS::GetVkLayout(GetLayout(range.baseArrayLayer, range.baseMipLevel));

    VkImageMemoryBarrier const barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout           = layout,
        .newLayout           = layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = m_state->m_textureImage,
        .subresourceRange    = range,
    };
    vkCmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
}

void VulkanTexture::AttachmentToSamplerBarrier(VulkanCommandBuffer* commands, const VkImageSubresourceRange& range) {
    VkCommandBuffer const cmdbuf = commands->Buffer();
    VkImageLayout const   layout = VK_UTILS::GetVkLayout(GetLayout(range.baseArrayLayer, range.baseMipLevel));

    VkImageMemoryBarrier const barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout           = layout,
        .newLayout           = layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = m_state->m_textureImage,
        .subresourceRange    = range,
    };
    vkCmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
}

void VulkanTexture::SetLayout(const VkImageSubresourceRange& range, VulkanLayout newLayout) {
    uint32_t const firstLayer = range.baseArrayLayer;
    uint32_t const lastLayer  = firstLayer + range.layerCount;
    uint32_t const firstLevel = range.baseMipLevel;
    uint32_t const lastLevel  = firstLevel + range.levelCount;

    assert_invariant(firstLevel <= 0xffff && lastLevel <= 0xffff);
    assert_invariant(firstLayer <= 0xffff && lastLayer <= 0xffff);

    // 键把 (layer, level) 压进一个 32 位整数，故两段都必须落在 16 位内
    for (uint32_t layer = firstLayer; layer < lastLayer; ++layer) {
        uint32_t const first = (layer << 16) | firstLevel;
        uint32_t const last  = (layer << 16) | lastLevel;
        if (newLayout == VulkanLayout::UNDEFINED) {
            m_state->m_subresourceLayouts.Clear(first, last);
        } else {
            m_state->m_subresourceLayouts.Add(first, last, newLayout);
        }
    }
}

void VulkanTexture::SetYcbcrConversion(VkSamplerYcbcrConversion conversion) {
    // 转换只由缓存创建，因此句柄相等即参数相等；转换变了则已缓存的图像视图全部失效
    VulkanTextureState::Ycbcr ycbcr = { .conversion = conversion };
    if (m_state->m_ycbcr != ycbcr) {
        m_state->m_ycbcr = ycbcr;
        m_state->clearCachedImageViews();
    }
}

VulkanLayout VulkanTexture::GetLayout(uint32_t layer, uint32_t level) const {
    assert_invariant(level <= 0xffff && layer <= 0xffff);
    uint32_t const key = (layer << 16) | level;
    if (!m_state->m_subresourceLayouts.Has(key)) {
        return VulkanLayout::UNDEFINED;
    }
    return m_state->m_subresourceLayouts.Get(key);
}

bool VulkanAttachment::IsDepth() const { return (texture->GetImageAspect() & VK_IMAGE_ASPECT_DEPTH_BIT) != 0; }

VkImage VulkanAttachment::GetImage() const { return texture ? texture->GetImage() : VK_NULL_HANDLE; }

VkFormat VulkanAttachment::GetFormat() const { return texture ? texture->GetFormat() : VK_FORMAT_UNDEFINED; }

VulkanLayout VulkanAttachment::GetLayout() const { return texture ? texture->GetLayout(layer, level) : VulkanLayout::UNDEFINED; }

VkExtent2D VulkanAttachment::GetExtent2D() const {
    assert_invariant(texture);
    return { std::max(1u, texture->width >> level), std::max(1u, texture->height >> level) };
}

VkImageView VulkanAttachment::GetImageView() {
    assert_invariant(texture);
    VkImageSubresourceRange const range = GetSubresourceRange();
    return texture->GetAttachmentView(range, range.layerCount > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
}

VkImageSubresourceRange VulkanAttachment::GetSubresourceRange() const {
    assert_invariant(texture);
    return {
        .aspectMask     = texture->GetImageAspect(),
        .baseMipLevel   = uint32_t(level),
        .levelCount     = 1,
        .baseArrayLayer = uint32_t(layer),
        .layerCount     = layerCount,
    };
}

END_NS_BACKEND
