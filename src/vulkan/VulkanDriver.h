#pragma once

#include "Backend/Driver.h"
#include "Backend/DriverDefine.h"
#include "VulkanContext.h"

BEGIN_NS_BACKEND

class VulkanPlatform;

class VulkanDriver : public Driver {
public:
    // 当前为占位实现，仅构造对象
    static DriverPtr Create(VulkanPlatform *platform, VulkanContext &context,
                            const DriverConfig &config);
};

END_NS_BACKEND