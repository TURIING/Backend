#pragma once

#include <string>

#include "Defines.h"
#include "VulInstance.h"
#include "VulObject.h"

BEGIN_NS_BACKEND

class VulPhysicalDevice;
DECLARE_SHARE_PTR_CLASS(VulPhysicalDevice);

// VkPhysicalDevice 封装：物理设备不销毁
class VulPhysicalDevice final : public VulObject<VkPhysicalDevice> {
    struct BuilderDetails;

public:
    class Builder : public NS_UTILS::BuilderBase<BuilderDetails> {
        friend struct VulPhysicalDevice::BuilderDetails;

    public:
        Builder() noexcept;
        ~Builder() noexcept;
        Builder &SetInstance(VulInstancePtr instance) noexcept;
        Builder &SetGPUPreference(std::string deviceName, int8_t index) noexcept;
        VulPhysicalDevicePtr Build();
    };

    explicit VulPhysicalDevice(VkPhysicalDevice device);

    // 从 instance 枚举出的设备中挑选满足要求者；供平台的可覆写选择钩子复用
    NODISCARD static VkPhysicalDevice Select(VkInstance instance, std::string deviceName, int8_t index);
};

END_NS_BACKEND