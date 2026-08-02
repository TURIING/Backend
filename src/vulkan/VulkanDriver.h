#pragma once

#include "Backend/Driver.h"
#include "Backend/DriverDefine.h"
#include "VulkanContext.h"

BEGIN_NS_BACKEND

class VulkanPlatform;

class VulkanDriver : public Driver {
public:
    /**
     * @brief 创建 Vulkan driver（当前为占位实现，仅构造对象）
     * @param platform 所属 VulkanPlatform
     * @param context  已初始化的 VulkanContext
     * @param config   驱动配置
     * @return 新建的 DriverPtr
     */
    static DriverPtr Create(VulkanPlatform *platform, VulkanContext &context,
                            const DriverConfig &config);
};

END_NS_BACKEND