#pragma once

#include <string>

#include "Defines.h"
#include "VulInstance.h"
#include "VulObject.h"

BEGIN_NS_BACKEND

class VulPhysicalDevice;
DECLARE_SHARE_PTR_CLASS(VulPhysicalDevice);  // 前置声明 Ptr（Builder::Build 返回类型需要）

// VkPhysicalDevice 封装：物理设备不销毁
class VulPhysicalDevice final : public VulObject<VkPhysicalDevice> {
    struct BuilderDetails;

public:
    /**
     * 创建信息（Builder 模式）
     */
    class Builder : public BuilderBase<BuilderDetails> {
        friend struct VulPhysicalDevice::BuilderDetails;

    public:
        Builder() noexcept;
        ~Builder() noexcept;
        Builder &SetInstance(VulInstancePtr instance) noexcept;
        Builder &SetGPUPreference(std::string deviceName, int8_t index) noexcept;
        VulPhysicalDevicePtr Build();
    };

    /**
     * @param device 选中的 VkPhysicalDevice
     */
    explicit VulPhysicalDevice(VkPhysicalDevice device);
};

END_NS_BACKEND