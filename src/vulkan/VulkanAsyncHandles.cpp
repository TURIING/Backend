#include "vulkan/VulkanAsyncHandles.h"

#include "vulkan/utils/Spirv.h"

#include "Utils/Log.h"
#include "Utils/Macro.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>

BEGIN_NS_BACKEND

namespace {

VkShaderStageFlags GetVkStage(ShaderStage stage) {
    switch (stage) {
        CASE_FROM_TO(ShaderStage::VERTEX, VK_SHADER_STAGE_VERTEX_BIT);
        CASE_FROM_TO(ShaderStage::FRAGMENT, VK_SHADER_STAGE_FRAGMENT_BIT);
        CASE_FROM_TO(ShaderStage::COMPUTE, VK_SHADER_STAGE_COMPUTE_BIT);
    }
    LOG_CRITICAL("unsupported shader stage {}", static_cast<int>(stage));
    return 0;
}

}  // namespace

PushConstantDescription::PushConstantDescription(Program const& program) : m_rangeCount(0) {
    uint32_t offset = 0;

    for (ShaderStage const stage : { ShaderStage::VERTEX, ShaderStage::FRAGMENT, ShaderStage::COMPUTE }) {
        auto const& constants = program.GetPushConstants(stage);
        if (constants.empty()) {
            continue;
        }

        auto& description = m_descriptions[static_cast<uint8_t>(stage)];
        description.types.reserve(constants.size());
        std::for_each(constants.cbegin(), constants.cend(), [&description](Program::PushConstant constant) {
            description.types.push_back(constant.type);
        });

        uint32_t const constantsSize = static_cast<uint32_t>(constants.size()) * kEntrySize;
        m_ranges[m_rangeCount++]     = {
            .stageFlags = GetVkStage(stage),
            .offset     = offset,
            .size       = constantsSize,
        };
        description.offset = offset;
        offset += constantsSize;
    }
}

void PushConstantDescription::Write(VkCommandBuffer cmdbuf, VkPipelineLayout layout, ShaderStage stage, uint8_t index,
                                    PushConstantVariant const& value) {
    uint32_t binaryValue = 0;

    auto const&    description = m_descriptions[static_cast<uint8_t>(stage)];
    [[maybe_unused]] auto const& types = description.types;
    uint32_t const offset      = description.offset;

    if (std::holds_alternative<bool>(value)) {
        LOG_ASSERT(types[index] == ConstantType::BOOL);
        binaryValue = static_cast<uint32_t>(std::get<bool>(value) ? VK_TRUE : VK_FALSE);
    } else if (std::holds_alternative<float>(value)) {
        LOG_ASSERT(types[index] == ConstantType::FLOAT);
        float const fval = std::get<float>(value);
        std::memcpy(&binaryValue, &fval, sizeof(binaryValue));
    } else {
        LOG_ASSERT(types[index] == ConstantType::INT);
        int const ival = std::get<int>(value);
        std::memcpy(&binaryValue, &ival, sizeof(binaryValue));
    }

    // push constant 以 uint32 为粒度写入，偏移由 stage 基址加常量下标算出
    vkCmdPushConstants(cmdbuf, layout, GetVkStage(stage), offset + index * kEntrySize, kEntrySize, &binaryValue);
}

VulkanProgram::VulkanProgram(VkDevice device, Program const& builder) noexcept
    : HwProgram(builder.GetName()),
      m_info(new (std::nothrow) PipelineInfo(builder)),
      m_device(device) {
    LOG_ASSERT(m_info != nullptr);

    Program::ShaderSource const& blobs = builder.GetShadersSource();
    auto&                        modules = m_info->shaders;
    auto const&                  specializationConstants = builder.GetSpecializationConstants();
    std::vector<uint32_t>        shader;

    static_assert(static_cast<ShaderStage>(0) == ShaderStage::VERTEX && static_cast<ShaderStage>(1) == ShaderStage::FRAGMENT &&
                  kMaxShaderModules == 2);

    for (size_t i = 0; i < kMaxShaderModules; i++) {
        Program::ShaderBlob const& blob = blobs[i];

        auto const* data     = reinterpret_cast<uint32_t const*>(blob.data());
        size_t      dataSize = blob.size();

        if (!specializationConstants.empty()) {
            VK_UTILS::WorkaroundSpecConstant(blob, specializationConstants, shader);
            data     = shader.data();
            dataSize = shader.size() * sizeof(uint32_t);
        }

        VkShaderModule&         module     = modules[i];
        VkShaderModuleCreateInfo moduleInfo = {
            .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = dataSize,
            .pCode    = data,
        };
        VkResult const result = vkCreateShaderModule(m_device, &moduleInfo, kVkAlloc, &module);
        if (result != VK_SUCCESS) {
            LOG_CRITICAL("Unable to create shader module. error={}", static_cast<int32_t>(result));
        }
    }
}

VulkanProgram::~VulkanProgram() {
    for (auto shader : m_info->shaders) {
        vkDestroyShaderModule(m_device, shader, kVkAlloc);
    }
    delete m_info;
}

void VulkanProgram::FlushPushConstants(VkPipelineLayout layout) {
    // 走到这里布局必然已确定，否则调用点判断有误
    LOG_ASSERT(layout != VK_NULL_HANDLE);
    for (auto const& constant : m_queuedPushConstants) {
        m_info->pushConstantDescription.Write(constant.cmdbuf, layout, constant.stage, constant.index, constant.value);
    }
    m_queuedPushConstants.clear();
}

void VulkanProgram::WritePushConstant(VkCommandBuffer cmdbuf, VkPipelineLayout layout, ShaderStage stage, uint8_t index,
                                      PushConstantVariant const& value) {
    // 用到外部采样器时 bindPipeline 会提前返回而不绑定布局，布局要到 draw 时才确定；
    // 这期间写入的 push constant 只能排队，等布局就绪后由 FlushPushConstants 补写
    if (layout != VK_NULL_HANDLE) {
        m_info->pushConstantDescription.Write(cmdbuf, layout, stage, index, value);
    } else {
        m_queuedPushConstants.push_back({ cmdbuf, stage, index, value });
    }
}

END_NS_BACKEND
