#pragma once

#include "Backend/DriverDefine.h"

#include <memory>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

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

    // 外部格式信息：用于为使用外部格式采样的材质预加载管线缓存
    struct ExternalYcbcrFormat {
        uint64_t                      externalFormat;
        VkSamplerYcbcrModelConversion ycbcrModelConversion;
        VkSamplerYcbcrRange           ycbcrRange;
    };

    // 平台定制项
    struct Customization {
        // GPU 偏好：可通过设备名子串或设备列表下标指定
        struct GPUPreference {
            std::string deviceName;
            int8_t      index = -1;
        } gpu;

        bool isSRGBSwapChainSupported = true;

        bool flushAndWaitOnWindowResize = true;                // 窗口尺寸变化时先 flush 并等待命令队列，再重建交换链

        bool transitionSwapChainImageLayoutForPresent = true;  // 交换链图像是否转换到适合呈现的布局

        uint32_t timeBeforeEvictionFbo = 3;                    // 未使用的 framebuffer 在多少帧后被逐出缓存

        // 并行预编译管线缓存时，按这些外部格式预热带不可变采样器的布局
        std::vector<ExternalYcbcrFormat> pipelineCachePrewarmExternalFormats;
    };

    using SwapChainPtr = Platform::SwapChain *;

    // 构成交换链的图像集合及其格式、尺寸
    struct SwapChainBundle {
        std::vector<VkImage> colors;
        VkImage              depth       = VK_NULL_HANDLE;
        VkFormat             colorFormat = VK_FORMAT_UNDEFINED;
        VkFormat             depthFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D           extent      = { 0, 0 };
        uint32_t             layerCount  = 1;
        bool                 isProtected = false;
    };

    struct ImageSyncData {
        static constexpr uint32_t INVALID_IMAGE_INDEX = UINT32_MAX;

        uint32_t    imageIndex          = INVALID_IMAGE_INDEX;  // vkAcquireNextImage 等返回的下一张图像的索引
        VkSemaphore imageReadySemaphore = VK_NULL_HANDLE;
    };

    // 平台特定的 surface 创建结果：headless 场景下 surface 为空、由 extent 提供虚拟尺寸
    using SurfaceBundle = std::tuple<VkSurfaceKHR, VkExtent2D>;

    VulkanPlatform() noexcept;
    ~VulkanPlatform() noexcept override;
    DriverPtr CreateDriver(const DriverConfig &config, void *shareContext) override;

    /**
     * @brief 返回交换链所需的实例扩展（如 Apple 平台的 VK_EXT_metal_surface）
     */
    virtual ExtensionSet GetSwapchainInstanceExtensions() const = 0;
    NODISCARD VkInstance GetVkInstance() const noexcept;
    NODISCARD VkPhysicalDevice GetVkPhysicalDevice() const noexcept;
    NODISCARD VkDevice GetVkDevice() const noexcept;
    NODISCARD uint32_t GetGraphicsQueueFamilyIndex() const noexcept;
    NODISCARD uint32_t GetGraphicsQueueIndex() const noexcept;
    NODISCARD VkQueue GetVkGraphicsQueue() const noexcept;
    virtual ExtensionSet GetRequiredInstanceExtensions() { return {}; }
    virtual Customization GetCustomization() const noexcept { return {}; }

    /**
     * @brief 基于平台原生窗口创建 VkSurfaceKHR
     * @param nativeWindow 平台原生窗口句柄（Apple 平台为 CAMetalLayer*）；为空时表示 headless
     * @param instance     已初始化的 VkInstance
     * @param flags        交换链创建标志
     * @return surface 句柄与尺寸；headless 时 surface 为 VK_NULL_HANDLE
     */
    virtual SurfaceBundle CreateVkSurfaceKHR(void *nativeWindow, VkInstance instance, uint64_t flags) const noexcept = 0;

    /**
     * @brief 创建交换链；nativeWindow 为空时按 extent 创建 headless 交换链
     * @param flags  交换链创建标志，取值见 DriverDefine.h 的 kSwapChainConfig* 常量
     * @param extent headless 交换链的尺寸
     */
    virtual SwapChainPtr CreateSwapChain(void *nativeWindow, uint64_t flags = 0, VkExtent2D extent = { 0, 0 });

    /**
     * @brief 取交换链的图像集合与格式；须在 CreateSwapChain() 或 Recreate() 之后调用
     */
    virtual SwapChainBundle GetSwapChainBundle(SwapChainPtr handle);

    /**
     * @brief 取下一张用于渲染的图像，索引写入 outImageSyncData
     */
    virtual VkResult Acquire(SwapChainPtr handle, ImageSyncData *outImageSyncData);

    /**
     * @brief 呈现 index 对应的图像；呈现前等待 finishedDrawing 信号量
     */
    virtual VkResult Present(SwapChainPtr handle, uint32_t index, VkSemaphore finishedDrawing);

    /**
     * @brief surface 尺寸是否已变化
     */
    virtual bool HasResized(SwapChainPtr handle);

    /**
     * @brief 交换链是否承载受保护内容
     */
    virtual bool IsProtected(SwapChainPtr handle);

    /**
     * @brief 重建交换链
     */
    virtual VkResult Recreate(SwapChainPtr handle);

    /**
     * @brief 销毁交换链
     */
    virtual void Destroy(SwapChainPtr handle);

    /**
     * @brief 创建 Platform::Sync 对象，承载给定围栏的状态
     */
    virtual Platform::Sync *CreateSync(std::shared_ptr<VulkanCmdFence> fenceStatus) noexcept override;

    /**
     * @brief 销毁由本平台 CreateSync 创建的同步对象
     */
    virtual void DestroySync(Platform::Sync *sync) noexcept override;

    /**
     * @brief 释放平台拥有的资源
     */
    virtual void Terminate();

    ExtensionSet GetInstanceExtensions(ExtensionSet const &externallyRequiredExts = {});

protected:
    // 平台侧 VulkanSync：驱动侧 Backend::VulkanSync（变更 6）是另一个类型，引用本类型须写全限定名
    struct VulkanSync : public Platform::Sync {
        std::shared_ptr<VulkanCmdFence> fenceStatus;
    };

    /**
     * @brief 创建 VkInstance；子类可覆写以定制层与扩展
     */
    virtual VkInstance CreateVkInstance(VkInstanceCreateInfo const &createInfo) noexcept;

    /**
     * @brief 选择 VkPhysicalDevice；子类可覆写以定制选择逻辑
     */
    virtual VkPhysicalDevice SelectVkPhysicalDevice(VkInstance instance) noexcept;

    /**
     * @brief 创建 VkDevice；子类可覆写以定制扩展与特性
     */
    virtual VkDevice CreateVkDevice(VkDeviceCreateInfo const &createInfo) noexcept;

private:
    // createLogicalDevice 请求的设备特性集合
    struct MiscDeviceFeatures {
        bool               dynamicRendering     = false;  //!< 允许创建无 render pass 的 VkGraphicsPipeline
        bool               imageView2Don3DImage = false;  //!< 允许从 3D VkImage 创建 2D image view
        GpuContextPriority gpuContextPriority   = GpuContextPriority::Default;
    };

    void initRuntime(void *shareContext);
    ExtensionSet initInstance();
    void selectPhysicalDevice(void *shareContext);
    ExtensionSet initDeviceExtensions(DriverConfig const &config, ExtensionSet &instExts);
    void createLogicalDevice(DriverConfig const &config, ExtensionSet &instExts, void *shareContext);
    void initQueues();
    void queryAndSetDeviceFeatures(DriverConfig const &driverConfig, ExtensionSet const &instExts, ExtensionSet const &deviceExts,
                                   void *sharedContext) noexcept;
};
DECLARE_SHARE_PTR_CLASS(VulkanPlatform);

END_NS_BACKEND
