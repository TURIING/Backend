#pragma once

#include "Utils/Utils.h"

#include <cstdint>
#include <functional>
#include <map>
#include <unordered_set>

#include "vulkan/VulkanContext.h"
#include "vulkan/resource/ResourceManager.h"
#include "vulkan/stage/VulkanStageBuffer.h"
#include "vulkan/commands/VulkanCommands.h"

BEGIN_NS_BACKEND


// 暂存图像：按 (格式, 宽, 高) 复用的线性平铺图像，作 blit 上传的中转
class VulkanStageImage {
public:
    // 池内图像的持有者：最后一份引用析构时经回调把图像交还空闲表
    class Resource : public ::Backend::Resource {
    public:
        using RecycleFn = std::function<void(VulkanStageImage*)>;

        Resource(VulkanStageImage* image, RecycleFn&& onRecycleFn)
            : m_image(image), m_onRecycleFn(std::move(onRecycleFn)) {}

        ~Resource() override {
            if (m_onRecycleFn) {
                m_onRecycleFn(m_image);
            }
        }

        NODISCARD VkFormat GetFormat() const { return m_image->GetFormat(); }
        NODISCARD uint32_t GetWidth() const { return m_image->GetWidth(); }
        NODISCARD uint32_t GetHeight() const { return m_image->GetHeight(); }
        NODISCARD VmaAllocation GetMemory() const { return m_image->GetMemory(); }
        NODISCARD VkImage GetImage() const { return m_image->GetImage(); }

    private:
        VulkanStageImage* const m_image;
        RecycleFn               m_onRecycleFn;
    };
    DECLARE_SHARE_PTR_CLASS(Resource);

    VulkanStageImage(VkFormat format, uint32_t width, uint32_t height, VmaAllocation memory, VkImage image, uint64_t lastAccessed)
        : m_format(format), m_width(width), m_height(height), m_memory(memory), m_image(image), m_lastAccessed(lastAccessed) {}

    VulkanStageImage(const VulkanStageImage&)            = delete;
    VulkanStageImage& operator=(const VulkanStageImage&) = delete;
    VulkanStageImage(VulkanStageImage&&)                 = delete;
    VulkanStageImage& operator=(VulkanStageImage&&)      = delete;

    NODISCARD VkFormat GetFormat() const { return m_format; }
    NODISCARD uint32_t GetWidth() const { return m_width; }
    NODISCARD uint32_t GetHeight() const { return m_height; }
    NODISCARD VmaAllocation GetMemory() const { return m_memory; }
    NODISCARD VkImage GetImage() const { return m_image; }
    NODISCARD uint64_t GetLastAccessed() const { return m_lastAccessed; }

private:
    const VkFormat      m_format;
    const uint32_t      m_width;
    const uint32_t      m_height;
    const VmaAllocation m_memory;
    const VkImage       m_image;

    uint64_t m_lastAccessed;

    // 淘汰时需更新 m_lastAccessed
    friend class VulkanStagePool;
};

template <>
ResourceType Resource::GetTypeEnum<VulkanStageImage::Resource>() const noexcept;

// 暂存缓冲池：按容量复用缓冲、按尺寸复用图像，并周期性回收长期未用的暂存块
DECLARE_CLASS_AND_SHARE_PTR(VulkanStagePool);

class VulkanStagePool : public NS_UTILS::Ref {
public:
    VulkanStagePool(const VulkanContextPtr& context, const ResourceManagerPtr& resourceManager, VmaAllocator allocator,
                    const VulkanCommandsPtr& commands);

    VulkanStagePool(const VulkanStagePool&)            = delete;
    VulkanStagePool& operator=(const VulkanStagePool&) = delete;

    // 非线程安全，仅驱动线程调用；alignment 为 0 表示不对齐偏移
    NODISCARD VulkanStageBuffer::SegmentPtr AcquireStage(uint32_t numBytes, uint32_t alignment = 0) noexcept;

    // 图像取出即为 TRANSFER_SRC 布局，调用方不得再把它转换到其它布局
    NODISCARD VulkanStageImage::ResourcePtr AcquireStageImage(PixelDataFormat format, PixelDataType type, uint32_t width,
                                                              uint32_t height);

    void Gc() noexcept;

    // 须在 VkDevice 仍存活、且全部 Segment 已回收后调用
    void Terminate() noexcept;

private:
    NODISCARD uint32_t alignToNonCoherentAtomSize(uint32_t numBytes) const noexcept;

    NODISCARD VulkanStageBufferPtr allocateNewStage(uint32_t capacity) noexcept;

    void destroyStage(VulkanStageBufferPtr& stage) noexcept;

    VulkanContextPtr   m_context;
    ResourceManagerPtr m_resourceManager;
    VmaAllocator       m_allocator;
    // 驱动在当前变更尚未持有 VulkanCommands（属变更 7 接线），为空时跳过图像布局转换
    VulkanCommandsPtr m_commands;

    // 剩余可切分空间 → 缓冲，lower_bound(numBytes) 命中容量足够的候选
    std::multimap<uint32_t, VulkanStageBufferPtr> m_stages;

    // 空闲图像；归还只在此登记，淘汰按 lastAccessed 的 LRU 判定
    std::unordered_set<VulkanStageImage*> m_freeImages;

    uint64_t m_currentFrame = 0;
};

END_NS_BACKEND
