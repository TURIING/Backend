#pragma once

#include "Backend/DriverDefine.h"
#include "Backend/platform/VulkanPlatform.h"

#include "VkDef.h"

#include <vector>

BEGIN_NS_BACKEND

struct VulkanCommandBuffer;
struct VulkanRenderPass;
struct VulkanRenderTarget;

DECLARE_SHARE_PTR_CLASS(VulkanRenderPass);
DECLARE_SHARE_PTR_CLASS(VulkanRenderTarget);

// 一次渲染通道的进行态：开始与结束之间缓存命令缓冲，并持有该通道的目标与通道对象
struct VulkanRenderPassContext {
    VulkanCommandBuffer  *commandBuffer = nullptr;
    VulkanRenderTargetPtr renderTarget{};
    VulkanRenderPassPtr   renderPass{};
    RenderPassParams      params         = {};
    int                   currentSubpass = 0;
};

// 设备/实例上下文的不变数据集合（实际句柄存储于 VulkanPlatform）。
class VulkanContext : public NS_UTILS::Ref {
public:
    static uint32_t SelectMemoryType(VkPhysicalDeviceMemoryProperties const &memoryProperties, uint32_t types,
                                     VkFlags reqs) {
        for (uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++) {
            if (types & 1) {
                if ((memoryProperties.memoryTypes[i].propertyFlags & reqs) == reqs) {
                    return i;
                }
            }
            types >>= 1;
        }
        return (uint32_t)VK_MAX_MEMORY_TYPES;
    }

    inline uint32_t SelectMemoryType(uint32_t types, VkFlags reqs) const {
        return SelectMemoryType(m_memoryProperties, types, reqs);
    }

    inline VkPhysicalDeviceLimits const &GetPhysicalDeviceLimits() const noexcept {
        return m_physicalDeviceProperties.properties.limits;
    }

    inline uint32_t GetPhysicalDeviceVendorId() const noexcept {
        return m_physicalDeviceProperties.properties.vendorID;
    }

    inline char const *GetPhysicalDeviceName() const noexcept {
        return m_physicalDeviceProperties.properties.deviceName;
    }

    inline char const *GetDriverName() const noexcept { return m_driverProperties.driverName; }

    inline char const *GetDriverInfo() const noexcept { return m_driverProperties.driverInfo; }

    inline VkExternalFenceHandleTypeFlags GetFenceExportFlags() const noexcept { return m_fenceExportFlags; }

    inline VkFormatList const &GetAttachmentDepthStencilFormats() const noexcept { return m_depthStencilFormats; }

    inline VkFormatList const &GetBlittableDepthStencilFormats() const noexcept {
        return m_blittableDepthStencilFormats;
    }

    inline bool IsMultiviewEnabled() const noexcept { return m_physicalDeviceVk11Features.multiview == VK_TRUE; }

    inline bool IsDebugMarkersSupported() const noexcept { return m_debugMarkersSupported; }

    inline bool IsDebugUtilsSupported() const noexcept { return m_debugUtilsSupported; }

    inline bool IsDynamicRenderingSupported() const noexcept {
        return m_dynamicRenderingFeatures.dynamicRendering == VK_TRUE;
    }

    inline bool IsImageView2DOn3DImageSupported() const noexcept {
        return m_portabilitySubsetFeatures.imageView2DOn3DImage == VK_TRUE;
    }

    inline bool IsLazilyAllocatedMemorySupported() const noexcept { return m_lazilyAllocatedMemorySupported; }

    inline bool IsProtectedMemorySupported() const noexcept { return m_protectedMemorySupported; }

    inline bool IsUnifiedMemoryArchitecture() const noexcept { return m_isUnifiedMemoryArchitecture; }

    inline bool IsVertexInputDynamicStateSupported() const noexcept { return m_vertexInputDynamicStateSupported; }

    inline bool IsGlobalPrioritySupported() const noexcept { return m_globalPrioritySupported; }

    inline bool IsDriverPropertiesSupported() const noexcept { return m_driverPropertiesSupported; }

    inline bool IsPipelineCreationFeedbackSupported() const noexcept { return m_pipelineCreationFeedbackSupported; }

    inline bool IsStagingBufferBypassEnabled() const noexcept { return m_stagingBufferBypassEnabled; }

    // 并行管线预编译：开启后 VulkanPipelineCache 会启动编译线程池
    inline bool IsPipelineCachePrewarmingEnabled() const noexcept { return m_asyncPipelineCachePrewarmingEnabled; }

    inline bool IsDepthClampSupported() const noexcept { return m_depthClampSupported; }

    inline bool IsClipDistanceSupported() const noexcept { return m_clipDistanceSupported; }

    inline bool IsImageCubeArraySupported() const noexcept { return m_imageCubeArraySupported; }

    // 为外部 YCbCr 格式做管线缓存预热时使用的格式清单
    inline std::vector<VulkanPlatform::ExternalYcbcrFormat> const& GetPipelineCachePrewarmExternalFormats() const noexcept {
        return m_pipelineCachePrewarmExternalFormats;
    }

private:
    VkPhysicalDeviceMemoryProperties m_memoryProperties         = {};
    VkPhysicalDeviceProperties2      m_physicalDeviceProperties = {
             .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
    };
    VkPhysicalDeviceDriverProperties m_driverProperties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
    };
    VkPhysicalDeviceVulkan11Features m_physicalDeviceVk11Features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
    };
    VkPhysicalDeviceFeatures2 m_physicalDeviceFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
    };
    VkPhysicalDeviceDynamicRenderingFeaturesKHR m_dynamicRenderingFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR,
    };
    VkPhysicalDevicePortabilitySubsetFeaturesKHR m_portabilitySubsetFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR,
        // 非 conformant 实现（如 MoltenVK）才填充该结构，默认假定该特性存在
        .imageView2DOn3DImage = VK_TRUE,
    };

    VkExternalFenceHandleTypeFlags m_fenceExportFlags = {};

    // 设备/实例支持与否的可选项
    bool m_debugMarkersSupported             = false;
    bool m_debugUtilsSupported               = false;
    bool m_isUnifiedMemoryArchitecture       = false;
    bool m_lazilyAllocatedMemorySupported    = false;
    bool m_pipelineCreationFeedbackSupported = false;
    bool m_protectedMemorySupported          = false;
    bool m_vertexInputDynamicStateSupported  = false;
    bool m_globalPrioritySupported           = false;
    bool m_driverPropertiesSupported         = false;
    bool m_depthClampSupported               = false;
    bool m_clipDistanceSupported             = false;
    bool m_imageCubeArraySupported           = false;

    // 应用层可开关的选项
    bool m_asyncPipelineCachePrewarmingEnabled = false;
    bool m_parallelShaderCompileDisabled       = false;
    bool m_stagingBufferBypassEnabled          = false;

    VkFormatList m_depthStencilFormats;
    VkFormatList m_blittableDepthStencilFormats;

    std::vector<VulkanPlatform::ExternalYcbcrFormat> m_pipelineCachePrewarmExternalFormats;

    friend class VulkanPlatform;
};

DECLARE_SHARE_PTR_CLASS(VulkanContext);
END_NS_BACKEND
