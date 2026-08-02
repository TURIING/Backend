#pragma once

#include <string>

#include "Backend/DriverDefine.h"
#include "Common.h"
#include "Platform.h"

BEGIN_NS_BACKEND
struct VulkanPlatformPrivate;

struct VulkanSharedContext {
    VkInstance       instance                 = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice           = VK_NULL_HANDLE;
    VkDevice         logicalDevice            = VK_NULL_HANDLE;
    uint32_t         graphicsQueueFamilyIndex = 0xFFFFFFFF;
    uint32_t         graphicsQueueIndex       = 0xFFFFFFFF;
    bool             debugUtilsSupported      = false;
    bool             debugMarkersSupported    = false;
    bool             multiviewSupported       = false;
};

class VulkanPlatform : public Platform, public NS_UTILS::Impl<VulkanPlatformPrivate> {
public:
    using ExtensionSet = std::unordered_set<std::string>;

    /**
     * 平台定制项
     */
    struct Customization {
        /**
         * GPU 偏好：可通过设备名子串或设备列表下标指定
         */
        struct GPUPreference {
            std::string deviceName;
            int8_t      index = -1;
        } gpu;

        bool isSRGBSwapChainSupported = true;
    };

    VulkanPlatform() noexcept;
    ~VulkanPlatform() noexcept;
    DriverPtr            CreateDriver(const DriverConfig &config, void *shareContext) override;
    virtual ExtensionSet getSwapchainInstanceExtensions() const = 0;

    /**
     * @brief 返回始终需要启用的实例扩展（默认无）
     */
    virtual ExtensionSet getRequiredInstanceExtensions() { return {}; }

    /**
     * @brief 返回平台定制项（GPU 偏好等）
     */
    virtual Customization getCustomization() { return {}; }

    /**
     * @brief 基于平台原生窗口创建 VkSurfaceKHR
     * @param nativeWindow 平台原生窗口句柄（Apple 平台为 CAMetalLayer*）
     * @param instance     已初始化的 VkInstance
     * @param flags        预留标志位
     * @return 创建的 VkSurfaceKHR 句柄
     */
    virtual VkSurfaceKHR createVkSurfaceKHR(void *nativeWindow, VkInstance instance, uint64_t flags) const noexcept = 0;

    /**
     * @brief 返回当前实例可用的扩展列表（过滤掉不可用的目标扩展）
     * @param externallyRequiredExts 上层额外要求的扩展
     * @return 实际可用的扩展名集合
     */
    ExtensionSet getInstanceExtensions(ExtensionSet const &externallyRequiredExts = {});

private:
    void initRuntime(void *shareContext);
    ExtensionSet initInstance();
    void selectPhysicalDevice(void *shareContext);
    ExtensionSet initDeviceExtensions(DriverConfig const &config, ExtensionSet &instExts);
    void createLogicalDevice(DriverConfig const &config, ExtensionSet &instExts, void *shareContext);
    void initQueues();
    void queryAndSetDeviceFeatures(DriverConfig const &driverConfig,
                                   ExtensionSet const &instExts, ExtensionSet const &deviceExts,
                                   void *sharedContext) noexcept;
};

END_NS_BACKEND
