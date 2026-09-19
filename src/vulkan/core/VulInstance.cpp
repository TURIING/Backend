#include "VulInstance.h"

#include "Utils/Log.h"

#include <cstring>
#include <string_view>
#include <vector>

#include "../VkDef.h"
#include "../VkUtils.h"

BEGIN_NS_BACKEND

namespace {

#if BVK_ENABLED(BVK_DEBUG_VALIDATION)
// 这些字符串需要分配在函数栈之外
constexpr std::string_view kDesiredLayers[] = {
    "VK_LAYER_KHRONOS_validation",
#if BVK_ENABLED(BVK_DEBUG_DUMP_API)
    "VK_LAYER_LUNARG_api_dump",
#endif
};

std::vector<const char *> GetEnabledLayers() {
    std::vector<VkLayerProperties> const availableLayers = VK_UTILS::enumerate(vkEnumerateInstanceLayerProperties);

    std::vector<const char *> enabledLayers;
    enabledLayers.reserve(sizeof(kDesiredLayers) / sizeof(kDesiredLayers[0]));
    for (auto const &desired : kDesiredLayers) {
        for (VkLayerProperties const &layer : availableLayers) {
            std::string_view const availableLayer(layer.layerName);
            if (availableLayer == desired) {
                enabledLayers.push_back(desired.data());
                break;
            }
        }
    }
    return enabledLayers;
}
#endif  // BVK_ENABLED(BVK_DEBUG_VALIDATION)

}  // namespace

struct VulInstance::BuilderDetails {
    VkApplicationInfo m_appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    };
    std::unordered_set<std::string> m_requiredExts;
    InstanceCreator                 m_instanceCreator;
};

VulInstance::VulInstance(VkInstance instance, bool shared) {
    m_pHandle = instance;
    m_shared  = shared;
}

VulInstance::~VulInstance() {
    // 共享实例由调用方管理生命周期，不在此销毁
    if (!m_shared && m_pHandle != VK_NULL_HANDLE) {
        vkDestroyInstance(m_pHandle, nullptr);
    }
}

VulInstance::Builder::~Builder() noexcept = default;

VulInstance::Builder::Builder() noexcept {
    m_pImpl->m_appInfo = {
        .sType       = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pEngineName = "Backend",
        .apiVersion  = VK_MAKE_API_VERSION(0, kRequiredVulkanVersionMajor, kRequiredVulkanVersionMinor, 0),
    };
}

VulInstance::Builder &VulInstance::Builder::SetApplicationInfo(VkApplicationInfo const &appInfo) noexcept {
    m_pImpl->m_appInfo = appInfo;
    return *this;
}

VulInstance::Builder &VulInstance::Builder::SetRequiredExtensions(std::unordered_set<std::string> const &exts) noexcept {
    m_pImpl->m_requiredExts = exts;
    return *this;
}

VulInstance::Builder &VulInstance::Builder::SetInstanceCreator(InstanceCreator creator) noexcept {
    m_pImpl->m_instanceCreator = std::move(creator);
    return *this;
}

VulInstancePtr VulInstance::Builder::Build() {
    VkInstanceCreateInfo instanceCreateInfo = {
        .sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &m_pImpl->m_appInfo,
    };
    bool validationFeaturesSupported = false;

#if BVK_ENABLED(BVK_DEBUG_VALIDATION)
    auto const enabledLayers = GetEnabledLayers();
    if (!enabledLayers.empty()) {
        // 若层可用，检查是否支持 VK_EXT_validation_features
        std::vector<VkExtensionProperties> const availableValidationExts =
            VK_UTILS::enumerate(vkEnumerateInstanceExtensionProperties, "VK_LAYER_KHRONOS_validation");
        for (auto const &extProps : availableValidationExts) {
            if (!strcmp(extProps.extensionName, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME)) {
                validationFeaturesSupported = true;
                break;
            }
        }
        instanceCreateInfo.enabledLayerCount   = (uint32_t)enabledLayers.size();
        instanceCreateInfo.ppEnabledLayerNames = enabledLayers.data();
    } else {
#if PLATFORM_ANDROID
        LOG_DEBUG("Validation layers are not available; did you set jniLibs in your gradle file?");
#else
        LOG_DEBUG("Validation layer not available; did you install the Vulkan SDK?");
#endif
    }
#endif  // BVK_ENABLED(BVK_DEBUG_VALIDATION)

    // Platform 可要求 1~2 个实例扩展，加上这里的公共代码最多 8 个
    constexpr uint32_t MAX_INSTANCE_EXTENSION_COUNT = 8;
    char const        *ppEnabledExtensions[MAX_INSTANCE_EXTENSION_COUNT];
    uint32_t           enabledExtensionCount = 0;

    if (validationFeaturesSupported) {
        ppEnabledExtensions[enabledExtensionCount++] = VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME;
    }
    for (auto const &requiredExt : m_pImpl->m_requiredExts) {
        LOG_ASSERT(enabledExtensionCount < MAX_INSTANCE_EXTENSION_COUNT);
        ppEnabledExtensions[enabledExtensionCount++] = requiredExt.data();
    }

    instanceCreateInfo.enabledExtensionCount   = enabledExtensionCount;
    instanceCreateInfo.ppEnabledExtensionNames = ppEnabledExtensions;
    if (m_pImpl->m_requiredExts.contains(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        instanceCreateInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    // Validation features
    VkValidationFeatureEnableEXT enables[] = {
        VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
    };
    VkValidationFeaturesEXT features = {
        .sType                         = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .enabledValidationFeatureCount = sizeof(enables) / sizeof(enables[0]),
        .pEnabledValidationFeatures    = enables,
    };
    if (validationFeaturesSupported) {
        VK_UTILS::chainStruct(&instanceCreateInfo, &features);
    }

    LOG_ASSERT(m_pImpl->m_instanceCreator);
    VkInstance instance = m_pImpl->m_instanceCreator(instanceCreateInfo);
    return new VulInstance(instance);
}

END_NS_BACKEND