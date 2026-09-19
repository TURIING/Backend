#include "vulkan/stage/VulkanStagePool.h"

#include "vulkan/VkDef.h"
#include "vulkan/VkUtils.h"
#include "vulkan/commands/VulkanCommands.h"
#include "vulkan/utils/Conversion.h"
#include "vulkan/utils/Image.h"

#include "Utils/Debug.h"
#include "Utils/Log.h"

#include <algorithm>
#include <utility>

BEGIN_NS_BACKEND

namespace {

constexpr uint32_t kStageSize              = 1 << 20;
constexpr uint32_t kMaxEmptyStagesToRetain = 1;
constexpr uint32_t kTimeBeforeEviction     = 3;

// 不假定 alignment 为 2 的幂，故不用 ALIGN_UP
uint32_t alignValue(uint32_t value, uint32_t alignment) {
    if (alignment == 0) {
        return value;
    }

    uint32_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

}  // namespace

VulkanStagePool::VulkanStagePool(const VulkanContextPtr& context, const ResourceManagerPtr& resourceManager, VmaAllocator allocator,
                                 const VulkanCommandsPtr& commands)
    : m_context(context), m_resourceManager(resourceManager), m_allocator(allocator), m_commands(commands) {}

VulkanStageImage::ResourcePtr VulkanStagePool::AcquireStageImage(PixelDataFormat format, PixelDataType type, uint32_t width,
                                                                 uint32_t height) {
    // 归还只登记进空闲表：池内不持有图像引用，生命周期完全由调用方的 Resource 引用决定
    auto wrapAsResource = [this](VulkanStageImage* image) {
        auto recycleFn = [this](VulkanStageImage* recycled) { m_freeImages.insert(recycled); };
        return m_resourceManager->AllocateAndConstruct<VulkanStageImage::Resource>(image, std::move(recycleFn));
    };

    VkFormat const vkformat = VK_UTILS::TransPixelDataFormatToVkFormat(format, type);
    for (auto stageImage : m_freeImages) {
        if (stageImage->GetFormat() == vkformat && stageImage->GetWidth() == width && stageImage->GetHeight() == height) {
            m_freeImages.erase(stageImage);
            stageImage->m_lastAccessed = m_currentFrame;
            return wrapAsResource(stageImage);
        }
    }

    VkImageCreateInfo const imageInfo = {
        .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType   = VK_IMAGE_TYPE_2D,
        .format      = vkformat,
        .extent      = { width, height, 1 },
        .mipLevels   = 1,
        .arrayLayers = 1,
        .samples     = VK_SAMPLE_COUNT_1_BIT,
        .tiling      = VK_IMAGE_TILING_LINEAR,
        .usage       = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    };

    VmaAllocationCreateInfo const allocInfo{
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
    };

    VkImage       image  = VK_NULL_HANDLE;
    VmaAllocation memory = VK_NULL_HANDLE;
    // 断言在 release 下为空，此处显式标注以避免未使用告警
    [[maybe_unused]] VkResult const result = vmaCreateImage(m_allocator, &imageInfo, &allocInfo, &image, &memory, nullptr);

    assert_invariant(result == VK_SUCCESS);

    if (m_commands != nullptr) {
        VkImageAspectFlags const aspectFlags = VK_UTILS::TransVkFormatToVkImageAspectFlags(vkformat);
        VkCommandBuffer const    cmdbuffer   = m_commands->Get().Buffer();

        // 图像随后会被 blit 到目标纹理，故直接进入 TRANSFER_SRC
        VK_UTILS::TransitionLayout(cmdbuffer, {
            .image        = image,
            .oldLayout    = VK_UTILS::VulkanLayout::UNDEFINED,
            .newLayout    = VK_UTILS::VulkanLayout::TRANSFER_SRC,
            .subresources = { aspectFlags, 0, 1, 0, 1 },
        });
    }

    auto* stageImage = new VulkanStageImage(vkformat, width, height, memory, image, m_currentFrame);

    return wrapAsResource(stageImage);
}

VulkanStageBuffer::SegmentPtr VulkanStagePool::AcquireStage(uint32_t numBytes, uint32_t alignment) noexcept {
    // 按原子大小上取整，保证 host flush 只覆盖本 Segment 涉及的原子
    numBytes = alignToNonCoherentAtomSize(numBytes);

    VulkanStageBufferPtr stage;
    uint32_t             segmentOffset = 0;

    auto iter = m_stages.lower_bound(numBytes);
    while (iter != m_stages.end()) {
        segmentOffset = alignValue(iter->second->GetCurrentOffset(), alignment);

        if (segmentOffset >= iter->second->GetCurrentOffset() && iter->second->GetCapacity() - numBytes >= segmentOffset) {
            stage = std::move(iter->second);
            m_stages.erase(iter);
            break;
        }

        ++iter;
    }

    if (!stage) {
        stage = allocateNewStage(std::max(numBytes, kStageSize));
    }

    auto segment = stage->AcquireSegment(m_resourceManager, segmentOffset, numBytes);

    uint32_t const spaceRemaining = stage->GetCapacity() - stage->GetCurrentOffset();
    m_stages.insert({ spaceRemaining, std::move(stage) });

    return segment;
}

void VulkanStagePool::Gc() noexcept {
    // 帧计数用于图像淘汰；缓冲淘汰只判空，前几帧跳过以保持与上游一致的节奏
    if (++m_currentFrame <= kTimeBeforeEviction) {
        return;
    }

    decltype(m_stages) freeStages;
    freeStages.swap(m_stages);

    uint8_t freeStageCount = 0;  // 假定空缓冲不会超过 255 个
    for (auto& pair : freeStages) {
        if (!pair.second->IsSafeToReset()) {
            m_stages.insert(std::move(pair));
            continue;
        }

        if (++freeStageCount > kMaxEmptyStagesToRetain) {
            destroyStage(pair.second);
            continue;
        }

        pair.second->Reset();
        uint32_t const capacity = pair.second->GetCapacity();
        m_stages.insert({ capacity, std::move(pair.second) });
    }

    // 若干帧未再被取用的图像直接销毁
    decltype(m_freeImages) freeImages;
    freeImages.swap(m_freeImages);
    uint64_t const evictionTime = m_currentFrame - kTimeBeforeEviction;
    for (auto* image : freeImages) {
        if (image->GetLastAccessed() < evictionTime) {
            vmaDestroyImage(m_allocator, image->GetImage(), image->GetMemory());
            delete image;
        } else {
            m_freeImages.insert(image);
        }
    }
}

void VulkanStagePool::Terminate() noexcept {
    for (auto& pair : m_stages) {
        destroyStage(pair.second);
    }
    m_stages.clear();

    for (auto* image : m_freeImages) {
        vmaDestroyImage(m_allocator, image->GetImage(), image->GetMemory());
        delete image;
    }
    m_freeImages.clear();
}

uint32_t VulkanStagePool::alignToNonCoherentAtomSize(uint32_t numBytes) const noexcept {
    return alignValue(numBytes, static_cast<uint32_t>(m_context->GetPhysicalDeviceLimits().nonCoherentAtomSize));
}

VulkanStageBufferPtr VulkanStagePool::allocateNewStage(uint32_t capacity) noexcept {
    VkBufferCreateInfo const bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = alignToNonCoherentAtomSize(capacity),
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };

    // 暂存缓冲需在 CPU 侧持久写入：显式声明 host 访问意图并预先映射，省去 map/unmap 往返
    VmaAllocationCreateInfo const allocInfo{
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VkBuffer          buffer = VK_NULL_HANDLE;
    VmaAllocation     memory = VK_NULL_HANDLE;
    VmaAllocationInfo allocationInfo{};
    VkResult const    result = vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &buffer, &memory, &allocationInfo);

#if BVK_ENABLED(BVK_DEBUG_STAGING_ALLOCATION)
    if (result != VK_SUCCESS) {
        LOG_ERROR("VulkanStagePool - failed to allocate a staging buffer of size {}, error: {}", capacity, result);
    } else {
        LOG_DEBUG("VulkanStagePool - allocated a staging buffer {} of size {}", static_cast<void const *>(buffer), capacity);
    }
#endif

    return NS_UTILS::MakeUnique<VulkanStageBuffer>(m_allocator, memory, buffer, capacity, allocationInfo.pMappedData);
}

void VulkanStagePool::destroyStage(VulkanStageBufferPtr& stage) noexcept { stage.Reset(); }

template <>
ResourceType Resource::GetTypeEnum<VulkanStageImage::Resource>() const noexcept {
    return ResourceType::StageImage;
}

END_NS_BACKEND
