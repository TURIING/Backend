#pragma once

#include "Backend/Namespace.h"
#include "Backend/Program.h"

#include <cstdint>
#include <vector>

BEGIN_NS_BACKEND

namespace VK_UTILS {

// 某些驱动无法正确处理 spec constant：有的用它做字段尺寸会编译失败，有的不会把
// spec-const 布尔量为 false 的分支做死代码消除。因此这里把 spec constant 直接改写成常量。
// 注意实现走「跳过指令」而非「改写 blob」：SPIR-V 校验器不认可 Nop（no-op）指令，
// 而 swiftshader 会在编译前校验着色器
void WorkaroundSpecConstant(Program::ShaderBlob const &blob,
                            Program::SpecializationConstantsInfo const &specConstants,
                            std::vector<uint32_t> &output);

}  // namespace VK_UTILS

END_NS_BACKEND
