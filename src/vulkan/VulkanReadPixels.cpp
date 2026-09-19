#include "vulkan/VulkanReadPixels.h"

#include "vulkan/VulkanHandle.h"
#include "vulkan/VulkanTexture.h"
#include "vulkan/utils/Conversion.h"  // TransVkFormatToPixelDataType / GetComponentCount
#include "vulkan/utils/Image.h"

#include "Utils/Log.h"
#include "Utils/Macro.h"
#include "Utils/Panic.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>

BEGIN_NS_BACKEND

namespace {

// 读回的目标内存布局由调用方给出的 PixelBufferDescriptor 决定，而 GPU 侧给出的是紧密排列的
// 数据，两者需要一次整形。
//
// 上游这一步由 DataReshaper 承担，而该前端件不在本路线图的移植范围内（变更 1 的既定决策，
// 变更 4 亦未引入第二份数据整形实现）。此处只覆盖读回最常见的场景：组件类型一致、通道数
// 一致，按目标行距/对齐逐行拷贝并遵守 left/top 偏移。需要跨类型或跨通道数转换时按原样拷贝
// 并告警——不静默产出错误布局。
uint32_t ComponentSize(PixelDataType type) {
    switch (type) {
        case PixelDataType::BYTE:
        case PixelDataType::UBYTE:
            return 1;
        case PixelDataType::SHORT:
        case PixelDataType::USHORT:
        case PixelDataType::HALF:
        case PixelDataType::USHORT_565:
            return 2;
        case PixelDataType::INT:
        case PixelDataType::UINT:
        case PixelDataType::FLOAT:
        case PixelDataType::UINT_10F_11F_11F_REV:
        case PixelDataType::UINT_2_10_10_10_REV:
            return 4;
        case PixelDataType::COMPRESSED:
            return 0;
    }
    return 0;
}

bool ChannelCountFromFormat(PixelDataFormat format, uint32_t* channelCount) {
    switch (format) {
        CASE_FROM_TO(PixelDataFormat::R, 1u);
        CASE_FROM_TO(PixelDataFormat::R_INTEGER, 1u);
        CASE_FROM_TO(PixelDataFormat::DEPTH_COMPONENT, 1u);
        CASE_FROM_TO(PixelDataFormat::ALPHA, 1u);
        CASE_FROM_TO(PixelDataFormat::RG, 2u);
        CASE_FROM_TO(PixelDataFormat::RG_INTEGER, 2u);
        CASE_FROM_TO(PixelDataFormat::DEPTH_STENCIL, 2u);
        CASE_FROM_TO(PixelDataFormat::RGB, 3u);
        CASE_FROM_TO(PixelDataFormat::RGB_INTEGER, 3u);
        CASE_FROM_TO(PixelDataFormat::UNUSED, 4u);
        CASE_FROM_TO(PixelDataFormat::RGBA, 4u);
        CASE_FROM_TO(PixelDataFormat::RGBA_INTEGER, 4u);
    }
    LOG_ERROR("readPixels: unsupported target PixelDataFormat {}", static_cast<int>(format));
    *channelCount = 0;
    return false;
}

bool ReshapeImage(PixelBufferDescriptor* dst, PixelDataType srcType, uint32_t srcChannelCount, uint8_t const* srcBytes,
                  int srcBytesPerRow, int width, int height, bool swizzle) {
    uint32_t dstChannelCount = 0;
    if (!ChannelCountFromFormat(dst->format, &dstChannelCount)) {
        return false;
    }
    if (ComponentSize(dst->type) == 0 || ComponentSize(srcType) == 0) {
        LOG_ERROR("readPixels: compressed formats cannot be reshaped");
        return false;
    }

    if (dst->type != srcType || dstChannelCount != srcChannelCount || (swizzle && dstChannelCount != 4)) {
        LOG_WARN("readPixels: reshaping {}-channel type {} into {}-channel type {} is not supported; rows are copied verbatim",
                 srcChannelCount, static_cast<int>(srcType), dstChannelCount, static_cast<int>(dst->type));
    }

    // 目标行距由格式/类型/步长/对齐共同决定，与 PixelBufferDescriptor 自身的约定保持一致
    size_t const dstRowStride = PixelBufferDescriptor::ComputeDataSize(
            dst->format, dst->type, dst->stride != 0 ? dst->stride : static_cast<size_t>(width), 1, dst->alignment);

    auto* dest = static_cast<uint8_t*>(dst->buffer) + static_cast<size_t>(dst->top) * dstRowStride +
                 static_cast<size_t>(dst->left) * dstChannelCount * ComponentSize(dst->type);

    size_t const minBytesPerRow = std::min(static_cast<size_t>(srcBytesPerRow), dstRowStride);
    for (int row = 0; row < height; ++row) {
        std::memcpy(dest, srcBytes, minBytesPerRow);
        srcBytes += srcBytesPerRow;
        dest += dstRowStride;
    }
    return true;
}

}  // namespace

using TaskHandler    = VulkanReadPixels::TaskHandler;
using WorkloadFunc   = TaskHandler::WorkloadFunc;
using OnCompleteFunc = TaskHandler::OnCompleteFunc;

TaskHandler::TaskHandler() : m_shouldStop(false), m_thread(&TaskHandler::Loop, this) {}

void TaskHandler::Post(WorkloadFunc&& workload, OnCompleteFunc&& onComplete) {
    LOG_ASSERT(!m_shouldStop);
    {
        std::unique_lock<std::mutex> lock(m_taskQueueMutex);
        m_taskQueue.push(std::make_pair(std::move(workload), std::move(onComplete)));
    }
    m_hasTaskCondition.notify_one();
}

void TaskHandler::Drain() {
    LOG_ASSERT(!m_shouldStop);

    // 借一个空任务体作为同步点：其完成回调执行时，排在前面的任务都已处理完。
    // 同步状态必须共享持有 —— 工作线程在 Drain() 返回后才跑到该回调是可能的，
    // 引用栈上变量会写坏已失效的栈帧
    struct SyncPoint {
        std::mutex              mutex;
        std::condition_variable condition;
        bool                    done = false;
    };
    auto const syncPoint = std::make_shared<SyncPoint>();
    Post([] {}, [syncPoint] {
        {
            std::lock_guard const lock(syncPoint->mutex);
            syncPoint->done = true;
        }
        syncPoint->condition.notify_one();
    });

    std::unique_lock lock(syncPoint->mutex);
    syncPoint->condition.wait(lock, [&syncPoint] { return syncPoint->done; });
}

void TaskHandler::Shutdown() {
    {
        std::unique_lock<std::mutex> lock(m_taskQueueMutex);
        m_shouldStop = true;
    }
    m_hasTaskCondition.notify_one();
    m_thread.join();
    FILAMENT_CHECK_POSTCONDITION(m_taskQueue.empty()) << "ReadPixels handler has tasks in the queue after shutdown";
}

void TaskHandler::Loop() {
    while (true) {
        std::unique_lock<std::mutex> lock(m_taskQueueMutex);
        m_hasTaskCondition.wait(lock, [this] { return !m_taskQueue.empty() || m_shouldStop; });
        if (m_shouldStop) {
            break;
        }
        auto [workload, onComplete] = m_taskQueue.front();
        m_taskQueue.pop();
        lock.unlock();
        workload();
        onComplete();
    }

    // 收尾：剩余任务的完成回调仍须执行，调用方才能释放资源
    while (true) {
        std::unique_lock<std::mutex> lock(m_taskQueueMutex);
        if (m_taskQueue.empty()) {
            break;
        }
        auto [workload, onComplete] = m_taskQueue.front();
        m_taskQueue.pop();
        lock.unlock();
        onComplete();
    }
}

VulkanReadPixels::VulkanReadPixels(VkDevice device) : m_device(device) {}

void VulkanReadPixels::Terminate() noexcept {
    LOG_ASSERT(m_device != VK_NULL_HANDLE);

    // 先停线程再销毁命令池：在途任务会用到命令池，反序会出现释放后使用
    if (m_taskHandler) {
        m_taskHandler->Shutdown();
        m_taskHandler.reset();
    }

    if (m_commandPool == VK_NULL_HANDLE) {
        return;
    }
    vkDestroyCommandPool(m_device, m_commandPool, kVkAlloc);
    m_commandPool = VK_NULL_HANDLE;
}

void VulkanReadPixels::Run(VulkanRenderTargetPtr srcTarget, uint32_t const x, uint32_t const y, uint32_t const width, uint32_t const height,
                           uint32_t const graphicsQueueFamilyIndex, PixelBufferDescriptor&& pbd,
                           SelectMemoryFunction const& selectMemoryFunc, OnReadCompleteFunction const& readCompleteFunc) {
    bool const isDepthStencil = pbd.format == PixelDataFormat::DEPTH_COMPONENT || pbd.format == PixelDataFormat::DEPTH_STENCIL;
    VulkanAttachment const srcAttachment = isDepthStencil ? srcTarget->GetDepthStencil() : srcTarget->GetColor(0);
    Run(srcAttachment.texture, srcAttachment.level, srcAttachment.layer, x, y, width, height, graphicsQueueFamilyIndex, std::move(pbd),
        selectMemoryFunc, readCompleteFunc);
}

void VulkanReadPixels::Run(VulkanTexturePtr srcTexture, uint8_t level, uint16_t layer, uint32_t x, uint32_t y, uint32_t width,
                           uint32_t height, uint32_t graphicsQueueFamilyIndex, PixelBufferDescriptor&& pbd,
                           SelectMemoryFunction const& selectMemoryFunc, OnReadCompleteFunction const& readCompleteFunc) {
    LOG_ASSERT(m_device != VK_NULL_HANDLE);
    LOG_ASSERT(static_cast<bool>(srcTexture));

    VkDevice& device = m_device;

    if (m_commandPool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo createInfo = {
            .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = graphicsQueueFamilyIndex,
        };
        vkCreateCommandPool(device, &createInfo, kVkAlloc, &m_commandPool);
    }

    // 只有真正调用 readPixels 时才起线程
    if (!m_taskHandler) {
        m_taskHandler = std::make_unique<TaskHandler>();
    }

    VkCommandPool const commandPool = m_commandPool;

    VkFormat const          srcFormat  = srcTexture->GetFormat();
    VkImageAspectFlags const aspectMask = VK_UTILS::TransVkFormatToVkImageAspectFlags(srcFormat);

    bool const swizzle = srcFormat == VK_FORMAT_B8G8R8A8_UNORM || srcFormat == VK_FORMAT_B8G8R8A8_SRGB;

    bool const isDepth = (aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) != 0 || (aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) != 0;

    VkImageAspectFlags copyAspect = aspectMask;
    if (isDepth) {
        // 请求 DEPTH_COMPONENT / DEPTH_STENCIL 时只取深度位：Vulkan 无法把交错的深度/模板
        // 数据经 vkCmdCopyImageToBuffer 拷进缓冲
        copyAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    uint32_t       componentCount = VK_UTILS::GetComponentCount(srcFormat);
    PixelDataType  componentType  = VK_UTILS::TransVkFormatToPixelDataType(srcFormat);

    if (isDepth) {
        // 深度/模板读回时 Vulkan 给出紧密排列的单分量数据
        componentCount = 1;
        if (srcFormat == VK_FORMAT_D16_UNORM) {
            componentType = PixelDataType::USHORT;
        } else if (copyAspect == VK_IMAGE_ASPECT_STENCIL_BIT) {
            componentType = PixelDataType::UBYTE;
        } else {
            componentType = (srcFormat == VK_FORMAT_D32_SFLOAT || srcFormat == VK_FORMAT_D32_SFLOAT_S8_UINT) ? PixelDataType::FLOAT
                                                                                                            : PixelDataType::UINT;
        }
    }

    uint32_t bpp = 0;
    switch (componentType) {
        case PixelDataType::UBYTE:
        case PixelDataType::BYTE:
            bpp = 1;
            break;
        case PixelDataType::USHORT:
        case PixelDataType::SHORT:
        case PixelDataType::HALF:
        case PixelDataType::USHORT_565:
            bpp = 2;
            break;
        case PixelDataType::UINT:
        case PixelDataType::INT:
        case PixelDataType::FLOAT:
        case PixelDataType::UINT_10F_11F_11F_REV:
        case PixelDataType::UINT_2_10_10_10_REV:
            bpp = 4;
            break;
        case PixelDataType::COMPRESSED:
            bpp = 1;  // 压缩格式的读回支持不完整
            break;
    }
    // 打包格式的字节宽度已含全部通道，不再乘以通道数
    if (componentType != PixelDataType::UINT_10F_11F_11F_REV && componentType != PixelDataType::USHORT_565 &&
        componentType != PixelDataType::UINT_2_10_10_10_REV && componentType != PixelDataType::COMPRESSED) {
        bpp *= componentCount;
    }

    uint32_t const samples = srcTexture->samples > 1 ? srcTexture->samples : 1;

    // 用 VkBuffer 而非线性平铺的 VkImage 作暂存：读回路径统一，且绕开部分实现对
    // 深度/模板格式禁用线性平铺的限制
    VkBuffer           stagingBuffer = VK_NULL_HANDLE;
    VkMemoryRequirements memReqs;

    uint32_t const   stagingSize = width * height * bpp * samples;
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = stagingSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };
    // 暂存缓冲池不可用：本路径在另一线程上，池是 backend 线程私有的
    vkCreateBuffer(device, &bufferInfo, kVkAlloc, &stagingBuffer);
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);

#if BVK_ENABLED(BVK_DEBUG_READ_PIXELS)
    LOG_DEBUG("readPixels created staging buffer to copy from image={} src-layout={}", static_cast<void const*>(srcTexture->GetImage()),
              static_cast<int>(srcTexture->GetLayout(layer, level)));
#endif

    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

    uint32_t memoryTypeIndex = selectMemoryFunc(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                                                        VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

    // 不支持 HOST_CACHED 时退化为 HOST_VISIBLE + HOST_COHERENT；HOST_CACHED 对读回性能影响很大
    if (memoryTypeIndex >= VK_MAX_MEMORY_TYPES) {
        memoryTypeIndex = selectMemoryFunc(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        LOG_WARN("readPixels is slow because VK_MEMORY_PROPERTY_HOST_CACHED_BIT is not available");
    }

    FILAMENT_CHECK_POSTCONDITION(memoryTypeIndex < VK_MAX_MEMORY_TYPES) << "VulkanReadPixels: unable to find a memory type that meets requirements.";

    VkMemoryAllocateInfo const allocInfo = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = memReqs.size,
        .memoryTypeIndex = memoryTypeIndex,
    };

    vkAllocateMemory(device, &allocInfo, kVkAlloc, &stagingMemory);
    vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);

    VkCommandBuffer             cmdBuffer = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo const allocateInfo = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = commandPool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(device, &allocateInfo, &cmdBuffer);

    VkCommandBufferBeginInfo const beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    VkImageSubresourceRange const srcRange = {
        .aspectMask     = aspectMask,
        .baseMipLevel   = level,
        .levelCount     = 1,
        .baseArrayLayer = layer,
        .layerCount     = 1,
    };
    // 注意本项目的签名是 (layer, level)，与上游同名函数的参数顺序相反
    VulkanLayout const srcLayout = srcTexture->GetLayout(layer, level);
    srcTexture->TransitionLayout(cmdBuffer, srcRange, VulkanLayout::TRANSFER_SRC);

    uint32_t const mipHeight = std::max(1u, srcTexture->height >> level);

    VkBufferImageCopy const region = {
        .bufferOffset      = 0,
        .bufferRowLength   = width,
        .bufferImageHeight = height,
        .imageSubresource  = {
            .aspectMask     = copyAspect,
            .mipLevel       = level,
            .baseArrayLayer = layer,
            .layerCount     = 1,
        },
        .imageOffset = {
            .x = static_cast<int32_t>(x),
            // 主机侧原点在左上，Vulkan 在左下
            .y = static_cast<int32_t>(mipHeight - (height + y)),
            .z = 0,
        },
        .imageExtent = {
            .width  = width,
            .height = height,
            .depth  = 1,
        },
    };

    // 只把指定 aspect 拷进紧密排列的暂存缓冲
    vkCmdCopyImageToBuffer(cmdBuffer, srcTexture->GetImage(), VK_UTILS::TransVulkanLayoutToVkImageLayout(VulkanLayout::TRANSFER_SRC), stagingBuffer, 1, &region);

    srcTexture->TransitionLayout(cmdBuffer, srcRange, srcLayout);

    vkEndCommandBuffer(cmdBuffer);

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &queue);
    VkFence                  readCompleteFence = VK_NULL_HANDLE;
    VkFenceCreateInfo const  fenceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    vkCreateFence(device, &fenceCreateInfo, kVkAlloc, &readCompleteFence);
    VkSubmitInfo const submitInfo{
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = 0,
        .pWaitSemaphores      = VK_NULL_HANDLE,
        .pWaitDstStageMask    = VK_NULL_HANDLE,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &cmdBuffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores    = VK_NULL_HANDLE,
    };
    vkQueueSubmit(queue, 1, &submitInfo, readCompleteFence);

    auto* const pUserBuffer = new PixelBufferDescriptor(std::move(pbd));
    auto        cleanPbdFunc = [pUserBuffer, readCompleteFunc]() {
        PixelBufferDescriptor& p = *pUserBuffer;
        readCompleteFunc(std::move(p));
        delete pUserBuffer;
    };
    auto waitFenceFunc = [device, width, height, swizzle, stagingBuffer, stagingMemory, commandPool, cmdBuffer, pUserBuffer, bpp, componentType,
                          componentCount, fence = readCompleteFence]() mutable {
        VkResult const status = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        if (status != VK_SUCCESS) {
            LOG_ERROR("Failed to wait for readPixels fence");
            return;
        }

        PixelBufferDescriptor& p = *pUserBuffer;

        void*          mapped      = nullptr;
        vkMapMemory(device, stagingMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
        uint8_t const* srcPixels = static_cast<uint8_t const*>(mapped);

        // MSAA 下 MoltenVK 按平面布局返回样本（样本 0 是开头 width * height 个像素），
        // 故直接按普通行距读取即可
        int const rowPitch = static_cast<int>(width * bpp);
        if (!ReshapeImage(&p, componentType, componentCount, srcPixels, rowPitch, static_cast<int>(width), static_cast<int>(height), swizzle)) {
            LOG_ERROR("Unsupported PixelDataFormat or PixelDataType");
        }

        vkUnmapMemory(device, stagingMemory);
        vkDestroyBuffer(device, stagingBuffer, kVkAlloc);
        vkFreeMemory(device, stagingMemory, kVkAlloc);
        vkDestroyFence(device, fence, kVkAlloc);
        vkFreeCommandBuffers(device, commandPool, 1, &cmdBuffer);
    };
    m_taskHandler->Post(std::move(waitFenceFunc), std::move(cleanPbdFunc));
}

void VulkanReadPixels::RunUntilComplete() {
    if (!m_taskHandler) {
        return;
    }
    m_taskHandler->Drain();
}

END_NS_BACKEND
