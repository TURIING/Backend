#pragma once

#include "Backend/DriverDefine.h"

BEGIN_NS_BACKEND

template <class T>
class VulObject : public NS_UTILS::Ref {
public:
    /**
     * @brief 获取原生句柄
     * @return 包装的 Vulkan 句柄
     */
    [[nodiscard]] T GetHandle() const { return m_pHandle; }

protected:
    T m_pHandle = nullptr;
};

END_NS_BACKEND