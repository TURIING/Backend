#pragma once

#include <functional>
#include <unordered_set>

#include "Defines.h"
#include "VulObject.h"

BEGIN_NS_BACKEND

class VulInstance;
DECLARE_SHARE_PTR_CLASS(VulInstance);  // 前置声明 Ptr（Builder::Build 返回类型需要）

// VkInstance 封装：非共享实例析构时自动销毁
class VulInstance final : public VulObject<VkInstance> {
    struct BuilderDetails;

public:
    // 实际的 vkCreateInstance 由平台注入，使平台子类可覆写实例创建而不必重写初始化流程
    using InstanceCreator = std::function<VkInstance(VkInstanceCreateInfo const &)>;

    // 创建信息（Builder 模式）
    class Builder : public NS_UTILS::BuilderBase<BuilderDetails> {
        friend struct VulInstance::BuilderDetails;

    public:
        Builder() noexcept;
        ~Builder() noexcept;
        Builder &SetApplicationInfo(VkApplicationInfo const &appInfo) noexcept;
        Builder &SetRequiredExtensions(std::unordered_set<std::string> const &exts) noexcept;
        Builder &SetInstanceCreator(InstanceCreator creator) noexcept;
        VulInstancePtr Build();
    };

    explicit VulInstance(VkInstance instance, bool shared = false);
    ~VulInstance() override;

private:
    bool m_shared = false;
};

END_NS_BACKEND