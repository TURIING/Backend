#include "vulkan/utils/Spirv.h"

#include "Utils/Debug.h"

#include <spirv/unified1/spirv.hpp>

#include <cstring>
#include <unordered_map>
#include <vector>

BEGIN_NS_BACKEND

namespace VK_UTILS {

namespace {

// 把 OpSpecConstant 指令就地改写成 OpConstant，并按程序给出的取值调整常量。
//
// 指令布局：第 1 字 = 字数与操作码，第 2 字 = 结果类型，第 3 字 = 结果 id，
// 第 4 字（仅整数/浮点有）= 字面量。只需改第 1 与第 4 字
void GetTransformedConstantInst(Program::SpecializationConstant const &value, uint32_t *inst) {
    constexpr size_t kOpWordIndex    = 0;
    constexpr size_t kValueWordIndex = 3;

    if (std::holds_alternative<bool>(value)) {
        inst[kOpWordIndex] = (3 << 16) |
                (std::get<bool>(value) ? spv::Op::OpConstantTrue : spv::Op::OpConstantFalse);
    } else if (std::holds_alternative<float>(value)) {
        float const fval = std::get<float>(value);
        inst[kOpWordIndex]    = (4 << 16) | spv::Op::OpConstant;
        inst[kValueWordIndex] = *reinterpret_cast<uint32_t const *>(&fval);
    } else {
        int const ival = std::get<int>(value);
        inst[kOpWordIndex]    = (4 << 16) | spv::Op::OpConstant;
        inst[kValueWordIndex] = *reinterpret_cast<uint32_t const *>(&ival);
    }
}

}  // namespace

void WorkaroundSpecConstant(Program::ShaderBlob const &blob,
                            Program::SpecializationConstantsInfo const &specConstants,
                            std::vector<uint32_t> &output) {
    using WordMap = std::unordered_map<uint32_t, uint32_t>;

    constexpr size_t kHeaderSize = 5;

    // 扫描过程形如：
    //     OpDecorate %1 SpecId 0
    //     %1 = OpSpecConstantFalse %bool
    // 需要记住变量 id 与 specId 的对应关系，在看到 %1 作为结果时把该指令换成取值已调整的
    // OpConstant；同时把 SpecId 装饰指令整条丢弃
    WordMap varToIdMap;

    size_t const dataSize = blob.size() / 4;

    output.resize(dataSize);
    uint32_t const *data       = reinterpret_cast<uint32_t const *>(blob.data());
    uint32_t       *outputData = output.data();

    std::memcpy(&outputData[0], &data[0], kHeaderSize * 4);
    size_t outputCursor = kHeaderSize;

    for (uint32_t cursor = kHeaderSize, cursorEnd = static_cast<uint32_t>(dataSize); cursor < cursorEnd;) {
        uint32_t const firstWord = data[cursor];
        uint32_t const wordCount = firstWord >> 16;
        uint32_t const op        = firstWord & 0x0000FFFF;

        switch (op) {
            case spv::Op::OpSpecConstant:
            case spv::Op::OpSpecConstantTrue:
            case spv::Op::OpSpecConstantFalse: {
                uint32_t const targetVar = data[cursor + 2];

                WordMap::const_iterator const idItr = varToIdMap.find(targetVar);
                bool const                    found = idItr != varToIdMap.end();
                assert_invariant(found && "Cannot find variable previously decorated with SpecId");
                (void) found;

                std::memcpy(&outputData[outputCursor], &data[cursor], wordCount * 4);
                // 未登记的 spec constant 多半来自前端与着色器的失配，此处保留原值以免越界
                if (found && idItr->second < specConstants.size()) {
                    GetTransformedConstantInst(specConstants[idItr->second], &outputData[outputCursor]);
                }
                outputCursor += wordCount;
                break;
            }
            case spv::Op::OpDecorate: {
                if (data[cursor + 2] == spv::Decoration::DecorationSpecId) {
                    uint32_t const targetVar = data[cursor + 1];
                    uint32_t const specId    = data[cursor + 3];
                    varToIdMap[targetVar]    = specId;
                    // SpecId 装饰无需写入输出
                    break;
                }
                [[fallthrough]];
            }
            default:
                std::memcpy(&outputData[outputCursor], &data[cursor], wordCount * 4);
                outputCursor += wordCount;
                break;
        }
        cursor += wordCount;
    }
    output.resize(outputCursor);
}

}  // namespace VK_UTILS

END_NS_BACKEND
