#pragma once

#include <unordered_set>

#include "../VkDef.h"
#include "Defines.h"
#include "VulObject.h"
#include "VulPhysicalDevice.h"

BEGIN_NS_BACKEND

class VulLogicDevice;
DECLARE_SHARE_PTR_CLASS(VulLogicDevice);  // 前置声明 Ptr（Builder::Build 返回类型需要）

// VkDevice 封装：非共享逻辑设备析构时自动销毁
class VulLogicDevice final : public VulObject<VkDevice> {
    struct BuilderDetails;

public:
    // 创建设备时应请求的额外特性
    struct MiscDeviceFeatures {
        bool dynamicRendering        = false; // 允许创建无 render pass 的 VkGraphicsPipeline
        bool imageView2Don3DImage    = false; // 允许从 3D VkImage 创建 2D image view
        GpuContextPriority gpuContextPriority = GpuContextPriority::DEFAULT;
    };

    // 创建信息（Builder 模式）
    class Builder : public NS_UTILS::BuilderBase<BuilderDetails> {
        friend struct VulLogicDevice::BuilderDetails;

    public:
        Builder() noexcept;
        ~Builder() noexcept;
        Builder &SetPhysicalDevice(VulPhysicalDevicePtr device) noexcept;
        Builder &SetDeviceExtensions(std::unordered_set<std::string> const &exts) noexcept;
        Builder &SetFeatures(VkPhysicalDeviceFeatures const &features) noexcept;
        Builder &SetVulkan11Features(VkPhysicalDeviceVulkan11Features const &features) noexcept;
        Builder &SetProtectedQueue(bool enabled) noexcept;
        Builder &SetRequestedFeatures(MiscDeviceFeatures const &features) noexcept;
        VulLogicDevicePtr Build();
    };

    explicit VulLogicDevice(VkDevice device, bool shared,
                            uint32_t graphicsQueueFamilyIndex, uint32_t graphicsQueueIndex,
                            uint32_t protectedGraphicsQueueFamilyIndex,
                            uint32_t protectedGraphicsQueueIndex);
    ~VulLogicDevice() override;

    uint32_t GetGraphicsQueueFamilyIndex() const noexcept { return m_graphicsQueueFamilyIndex; }
    uint32_t GetGraphicsQueueIndex() const noexcept { return m_graphicsQueueIndex; }
    uint32_t GetProtectedGraphicsQueueFamilyIndex() const noexcept {
        return m_protectedGraphicsQueueFamilyIndex;
    }
    uint32_t GetProtectedGraphicsQueueIndex() const noexcept {
        return m_protectedGraphicsQueueIndex;
    }

private:
    uint32_t m_graphicsQueueFamilyIndex          = INVALID_VK_INDEX;
    uint32_t m_graphicsQueueIndex                = INVALID_VK_INDEX;
    uint32_t m_protectedGraphicsQueueFamilyIndex = INVALID_VK_INDEX;
    uint32_t m_protectedGraphicsQueueIndex       = INVALID_VK_INDEX;
    bool     m_shared                            = false;
};

END_NS_BACKEND