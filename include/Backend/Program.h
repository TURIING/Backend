#pragma once

#include "Backend/DriverDefine.h"

#include "Utils/Macro.h"
#include "Utils/string/String.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

BEGIN_NS_BACKEND

/**
 * 着色器二进制的容器与管线创建的输入契约。
 *
 * Program 由前端构造后经 createProgram 传入后端，后端据此创建 VkShaderModule 与 VkPipeline；
 * 本层不涉及任何着色器编译。
 */
class Program {
public:
    static constexpr size_t SHADER_TYPE_COUNT  = 3;
    static constexpr size_t UNIFORM_BINDING_COUNT = CONFIG_UNIFORM_BINDING_COUNT;
    static constexpr size_t SAMPLER_BINDING_COUNT = CONFIG_SAMPLER_BINDING_COUNT;

    struct Descriptor {
        NS_UTILS::String     name;
        DescriptorType       type;
        descriptor_binding_t binding;
    };

    struct DescriptorSetLayoutBinding {
        descriptor_set_t      set;
        DescriptorSetLayout   layout;
    };

    using SpecializationConstant = std::variant<int32_t, float, bool>;
    using DescriptorSetLayoutArray = std::vector<DescriptorSetLayoutBinding>;

    struct Uniform {
        NS_UTILS::String name;    //!< uniform 字段的完整限定名
        uint16_t         offset;  //!< 在 uniform buffer 中的偏移（单位 uint32_t）
        uint8_t          size;    //!< 数组元素个数，非数组为 1
        UniformType      type;
    };

    using DescriptorBindingsInfo = std::vector<Descriptor>;
    using DescriptorSetInfo = std::array<DescriptorBindingsInfo, MAX_DESCRIPTOR_SET_COUNT>;
    using SpecializationConstantsInfo = std::vector<SpecializationConstant>;
    using ShaderBlob = std::vector<uint8_t>;
    using ShaderSource = std::array<ShaderBlob, SHADER_TYPE_COUNT>;

    using AttributesInfo = std::vector<std::pair<NS_UTILS::String, uint8_t>>;
    using UniformInfo = std::vector<Uniform>;
    using BindingUniformsInfo = std::vector<std::tuple<uint8_t, NS_UTILS::String, UniformInfo>>;

    struct PushConstant {
        NS_UTILS::String name;
        ConstantType     type;
    };

    Program() noexcept;

    Program(Program const& rhs)            = delete;
    Program& operator=(Program const& rhs) = delete;

    Program(Program&& rhs) noexcept;
    Program& operator=(Program&& rhs) noexcept;

    ~Program() noexcept;

    Program& PriorityQueue(CompilerPriorityQueue priorityQueue) noexcept;

    // 形参名与类型同名会遮蔽类型，故类型写全限定名
    Program& ShaderLanguage(Backend::ShaderLanguage shaderLanguage) noexcept;

    //! 按 stage 写入着色器二进制；字符串形式的着色器以空字符结尾，故 size 需含结尾空字符
    Program& Shader(ShaderStage stage, void const* data, size_t size);

    Program& DescriptorBindings(descriptor_set_t set, DescriptorBindingsInfo const& descriptorBindings) noexcept;

    Program& DescriptorLayout(descriptor_set_t set, DescriptorSetLayout const& descriptorLayout) noexcept;

    Program& Uniforms(uint32_t index, NS_UTILS::String const& name, UniformInfo const& uniforms);

    Program& Attributes(AttributesInfo const& attributes) noexcept;

    Program& SpecializationConstants(SpecializationConstantsInfo const& specConstants) noexcept;

    Program& PushConstants(ShaderStage stage, std::vector<PushConstant> const& constants) noexcept;

    Program& CacheId(uint64_t cacheId) noexcept;

    Program& Multiview(bool multiview) noexcept;

    NODISCARD ShaderSource const& GetShadersSource() const noexcept { return mShadersSource; }
    NODISCARD ShaderSource& GetShadersSource() noexcept { return mShadersSource; }

    NODISCARD NS_UTILS::String const& GetName() const noexcept { return mName; }
    NODISCARD NS_UTILS::String& GetName() noexcept { return mName; }

    NODISCARD Backend::ShaderLanguage GetShaderLanguage() const noexcept { return mShaderLanguage; }

    NODISCARD uint64_t GetCacheId() const noexcept { return mCacheId; }

    NODISCARD bool IsMultiview() const noexcept { return mMultiview; }

    NODISCARD CompilerPriorityQueue GetPriorityQueue() const noexcept { return mPriorityQueue; }

    NODISCARD SpecializationConstantsInfo const& GetSpecializationConstants() const noexcept {
        return mSpecializationConstants;
    }

    NODISCARD DescriptorSetInfo& GetDescriptorBindings() noexcept { return mDescriptorBindings; }

    NODISCARD DescriptorSetLayoutArray const& GetDescriptorSetLayouts() const noexcept {
        return mDescriptorLayouts;
    }

    NODISCARD std::vector<PushConstant> const& GetPushConstants(ShaderStage stage) const noexcept {
        return mPushConstants[static_cast<uint8_t>(stage)];
    }

    NODISCARD std::vector<PushConstant>& GetPushConstants(ShaderStage stage) noexcept {
        return mPushConstants[static_cast<uint8_t>(stage)];
    }

    NODISCARD BindingUniformsInfo const& GetBindingUniformInfo() const noexcept {
        return mBindingUniformsInfo;
    }

    NODISCARD BindingUniformsInfo& GetBindingUniformInfo() noexcept { return mBindingUniformsInfo; }

    NODISCARD AttributesInfo const& GetAttributes() const noexcept { return mAttributes; }

    NODISCARD AttributesInfo& GetAttributes() noexcept { return mAttributes; }

private:
    ShaderSource            mShadersSource;
    Backend::ShaderLanguage mShaderLanguage = Backend::ShaderLanguage::ESSL3;
    NS_UTILS::String        mName;
    uint64_t                mCacheId{};
    CompilerPriorityQueue   mPriorityQueue = CompilerPriorityQueue::High;
    SpecializationConstantsInfo mSpecializationConstants;
    std::array<std::vector<PushConstant>, SHADER_TYPE_COUNT> mPushConstants;
    DescriptorSetInfo       mDescriptorBindings;

    DescriptorSetLayoutArray mDescriptorLayouts;  // 本 Program 可能用到的 descriptor set 布局，便于提前编译管线

    AttributesInfo      mAttributes;
    BindingUniformsInfo mBindingUniformsInfo;

    bool mMultiview = false;  // ! 引擎是否以 multiview 立体初始化且本变体含 STE 标记
};

END_NS_BACKEND
