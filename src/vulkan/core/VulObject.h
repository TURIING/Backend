#pragma once

#include "Backend/DriverDefine.h"

BEGIN_NS_BACKEND

template <class T>
class VulObject : public NS_UTILS::Ref {
public:
    [[nodiscard]] T GetHandle() const { return m_pHandle; }

protected:
    T m_pHandle = nullptr;
};

END_NS_BACKEND