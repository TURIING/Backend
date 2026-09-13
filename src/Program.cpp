#include "Backend/Program.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

BEGIN_NS_BACKEND

// 构造与析构放在 .cpp 中，避免被内联进每个构造点
Program::Program() noexcept {}

Program::Program(Program&& rhs) noexcept = default;

Program& Program::operator=(Program&& rhs) noexcept = default;

Program::~Program() noexcept = default;

Program& Program::PriorityQueue(CompilerPriorityQueue const priorityQueue) noexcept {
    mPriorityQueue = priorityQueue;
    return *this;
}

Program& Program::ShaderLanguage(Backend::ShaderLanguage const shaderLanguage) noexcept {
    mShaderLanguage = shaderLanguage;
    return *this;
}

Program& Program::Shader(ShaderStage const stage, void const* data, size_t const size) {
    ShaderBlob blob(size);
    std::copy_n(static_cast<uint8_t const*>(data), size, blob.data());
    mShadersSource[static_cast<size_t>(stage)] = std::move(blob);
    return *this;
}

Program& Program::DescriptorBindings(descriptor_set_t const set,
        DescriptorBindingsInfo const& descriptorBindings) noexcept {
    mDescriptorBindings[set] = descriptorBindings;
    return *this;
}

Program& Program::DescriptorLayout(descriptor_set_t const set,
        DescriptorSetLayout const& descriptorLayout) noexcept {
    mDescriptorLayouts.push_back({ .set = set, .layout = descriptorLayout });
    return *this;
}

Program& Program::Uniforms(uint32_t const index, NS_UTILS::String const& name,
        UniformInfo const& uniforms) {
    mBindingUniformsInfo.emplace_back(index, name, uniforms);
    return *this;
}

Program& Program::Attributes(AttributesInfo const& attributes) noexcept {
    mAttributes = attributes;
    return *this;
}

Program& Program::SpecializationConstants(SpecializationConstantsInfo const& specConstants) noexcept {
    mSpecializationConstants = specConstants;
    return *this;
}

Program& Program::PushConstants(ShaderStage const stage,
        std::vector<PushConstant> const& constants) noexcept {
    mPushConstants[static_cast<uint8_t>(stage)] = constants;
    return *this;
}

Program& Program::CacheId(uint64_t const cacheId) noexcept {
    mCacheId = cacheId;
    return *this;
}

Program& Program::Multiview(bool const multiview) noexcept {
    mMultiview = multiview;
    return *this;
}

END_NS_BACKEND
