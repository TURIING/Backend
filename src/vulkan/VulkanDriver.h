#pragma once

#include "Backend/Driver.h"
#include "Backend/DriverDefine.h"
#include "Backend/platform/Platform.h"
#include "Backend/platform/VulkanPlatform.h"

#include "Utils/Utils.h"

#include "VulkanContext.h"
#include "buffer/VulkanBufferCache.h"
#include "stage/VulkanStagePool.h"

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
    ~VulkanDriver() noexcept override;

    static DriverPtr Create(VulkanPlatform *platform, const VulkanContextPtr &context, const DriverConfig &config);

    Dispatcher GetDispatcher() const noexcept override;

    template <typename T>
    friend class ConcreteDispatcher;

#define DECL_DRIVER_API(methodName, paramsDecl, params)                      inline void methodName(paramsDecl);
#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params) RetType methodName(paramsDecl) override;
#define DECL_DRIVER_API_RETURN(RetType, methodName, paramsDecl, params) \
    RetType methodName##S() noexcept override;                          \
    inline void methodName##R(RetType, paramsDecl);

#include "Backend/DriverAPI.inc"

private:
    void DestroyResources() noexcept;

    ResourceManagerPtr   m_resMgr;
    VulkanContextPtr     m_context;
    VmaAllocator         m_allocator = VK_NULL_HANDLE;
    VulkanBufferCachePtr m_bufferCache;
    VulkanStagePoolPtr   m_stagePool;
};

END_NS_BACKEND