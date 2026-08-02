#include "VulPhysicalDevice.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

#include "../VkDef.h"
#include "../VkUtils.h"

BEGIN_NS_BACKEND

namespace {

// 设备类型的偏好排序
inline int DeviceTypeOrder(VkPhysicalDeviceType deviceType) {
    constexpr std::array<VkPhysicalDeviceType, 5> TYPES = {
        VK_PHYSICAL_DEVICE_TYPE_OTHER,
        VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU,
        VK_PHYSICAL_DEVICE_TYPE_CPU,
        VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
        VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
    };
    if (auto itr = std::find(TYPES.begin(), TYPES.end(), deviceType); itr != TYPES.end()) {
        return std::distance(TYPES.begin(), itr);
    }
    return -1;
}

}  // namespace

// Builder 配置数据
struct VulPhysicalDevice::BuilderDetails {
    VulInstancePtr m_instance;
    std::string    m_deviceName;
    int8_t         m_index = -1;
};

VulPhysicalDevice::VulPhysicalDevice(VkPhysicalDevice device) {
    m_pHandle = device;
}

VulPhysicalDevice::Builder::Builder() noexcept = default;
VulPhysicalDevice::Builder::~Builder() noexcept = default;

VulPhysicalDevice::Builder &VulPhysicalDevice::Builder::SetInstance(VulInstancePtr instance) noexcept {
    m_pImpl->m_instance = std::move(instance);
    return *this;
}

VulPhysicalDevice::Builder &VulPhysicalDevice::Builder::SetGPUPreference(
        std::string deviceName, int8_t index) noexcept {
    m_pImpl->m_deviceName = std::move(deviceName);
    m_pImpl->m_index      = index;
    return *this;
}

VulPhysicalDevicePtr VulPhysicalDevice::Builder::Build() {
    std::vector<VkPhysicalDevice> const physicalDevices =
        VK_UTILS::enumerate(vkEnumeratePhysicalDevices, m_pImpl->m_instance->GetHandle());

    struct DeviceInfo {
        VkPhysicalDevice    device      = VK_NULL_HANDLE;
        VkPhysicalDeviceType deviceType = VK_PHYSICAL_DEVICE_TYPE_OTHER;
        int8_t              index       = -1;
        std::string_view    name;
    };
    std::vector<DeviceInfo> deviceList(physicalDevices.size());

    for (size_t deviceInd = 0; deviceInd < physicalDevices.size(); ++deviceInd) {
        auto const candidateDevice = physicalDevices[deviceInd];
        VkPhysicalDeviceProperties targetDeviceProperties;
        vkGetPhysicalDeviceProperties(candidateDevice, &targetDeviceProperties);

        int const major = VK_VERSION_MAJOR(targetDeviceProperties.apiVersion);
        int const minor = VK_VERSION_MINOR(targetDeviceProperties.apiVersion);

        // 设备是否满足所需 Vulkan 版本
        if (major < kRequiredVulkanVersionMajor) {
            continue;
        }
        if (major == kRequiredVulkanVersionMajor && minor < kRequiredVulkanVersionMinor) {
            continue;
        }

        // 设备是否有支持 graphics 的命令队列
        if (VK_UTILS::IdentifyGraphicsQueueFamilyIndex(candidateDevice, VK_QUEUE_GRAPHICS_BIT) ==
            INVALID_VK_INDEX) {
            continue;
        }

        // 设备是否支持 VK_KHR_swapchain 扩展
        std::vector<VkExtensionProperties> const extensions = VK_UTILS::enumerate(
            vkEnumerateDeviceExtensionProperties, candidateDevice,
            static_cast<char const *>(nullptr) /* pLayerName */);
        bool const supportsSwapchain =
            std::any_of(extensions.begin(), extensions.end(), [](auto const &ext) {
                return !strcmp(ext.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
            });
        if (!supportsSwapchain) {
            continue;
        }

        deviceList[deviceInd] = {
            .device     = candidateDevice,
            .deviceType = targetDeviceProperties.deviceType,
            .index      = (int8_t)deviceInd,
            .name       = targetDeviceProperties.deviceName,
        };
    }

    LOG_ASSERT(m_pImpl->m_index < static_cast<int32_t>(deviceList.size()));

    // 对设备列表排序
    std::sort(deviceList.begin(), deviceList.end(),
        [this](DeviceInfo const &a, DeviceInfo const &b) {
            if (b.device == VK_NULL_HANDLE) {
                return false;
            }
            if (a.device == VK_NULL_HANDLE) {
                return true;
            }
            if (!m_pImpl->m_deviceName.empty()) {
                if (a.name.find(m_pImpl->m_deviceName.c_str()) != a.name.npos) {
                    return false;
                }
                if (b.name.find(m_pImpl->m_deviceName.c_str()) != b.name.npos) {
                    return true;
                }
            }
            if (m_pImpl->m_index == a.index) {
                return false;
            }
            if (m_pImpl->m_index == b.index) {
                return true;
            }
            return DeviceTypeOrder(a.deviceType) < DeviceTypeOrder(b.deviceType);
        });
    auto device = deviceList.back().device;
    LOG_ASSERT(device != VK_NULL_HANDLE);
    return VulPhysicalDevicePtr(new VulPhysicalDevice(device));
}

END_NS_BACKEND