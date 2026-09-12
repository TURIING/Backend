#pragma once
#include "Backend/Driver.h"

#include "../DriverDefine.h"
BEGIN_NS_BACKEND

DECLARE_CLASS_AND_SHARE_PTR(Platform);

class UTILS_PUBLIC Platform : public utils::Ref {
public:
    virtual DriverPtr CreateDriver(const DriverConfig &config, void *shareContext) = 0;
};
END_NS_BACKEND
