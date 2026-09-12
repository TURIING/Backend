#pragma once

#include "Backend/Driver.h"
#include "Backend/DriverDefine.h"
#include "Backend/platform/Platform.h"
#include "Backend/platform/VulkanPlatform.h"

#include "Utils/Utils.h"

#include "VulkanContext.h"

#undef DECL_DRIVER_API
#undef DECL_DRIVER_API_SYNCHRONOUS
#undef DECL_DRIVER_API_RETURN
BEGIN_NS_BACKEND

DECLARE_CLASS_AND_SHARE_PTR(VulkanPlatform);
DECLARE_CLASS_AND_SHARE_PTR(VulkanContext);
DECLARE_CLASS_AND_SHARE_PTR(ResourceManager);

class VulkanDriver : public Driver {
public:
    VulkanDriver(const VulkanPlatformPtr &platform, const VulkanContextPtr &context, const DriverConfig &config);
    static DriverPtr Create(VulkanPlatform *platform, VulkanContext &context, const DriverConfig &config);

    Dispatcher GetDispatcher() const noexcept override;

    template <typename T>
    friend class ConcreteDispatcher;

#define DECL_DRIVER_API(methodName, paramsDecl, params)                      inline void methodName(paramsDecl);
#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params) RetType methodName(paramsDecl) override;
#define DECL_DRIVER_API_RETURN(RetType, methodName, paramsDecl, params) \
    RetType     methodName##S() noexcept override;                      \
    inline void methodName##R(RetType, paramsDecl);

#include "Backend/DriverAPI.inc"

private:
    ResourceManagerPtr m_resMgr;
};

END_NS_BACKEND