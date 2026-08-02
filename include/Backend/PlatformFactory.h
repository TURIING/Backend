#pragma once
#include "DriverDefine.h"
#include "platform/Platform.h"
BEGIN_NS_BACKEND

DECLARE_CLASS_AND_SHARE_PTR(Platform);

class UTILS_PUBLIC PlatformFactory {
public:
    static PlatformPtr Create(BackendType type);
    static void Destroy(PlatformPtr platform);
};

END_NS_BACKEND