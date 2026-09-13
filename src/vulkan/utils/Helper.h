#pragma once

#include "Backend/Namespace.h"

#include <volk.h>

// 上游本文件还带一组 enumerate() 模板与 EXPAND_ENUM 宏，本项目 VkUtils.h 已有一份功能等价、
// 且改用 CALL_VK 报错的实现，同名同签名，故此处不再重复提供

BEGIN_NS_BACKEND

namespace VK_UTILS {

// 各分量都是整数，无需引入 epsilon
inline bool Equivalent(VkRect2D const &a, VkRect2D const &b) {
    return a.extent.width == b.extent.width && a.extent.height == b.extent.height &&
           a.offset.x == b.offset.x && a.offset.y == b.offset.y;
}

inline bool Equivalent(VkExtent2D const &a, VkExtent2D const &b) {
    return a.height == b.height && a.width == b.width;
}

}  // namespace VK_UTILS

END_NS_BACKEND
