#pragma once

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
    /**
     * 创建信息（Builder 模式）
     */
    class Builder : public BuilderBase<BuilderDetails> {
        friend struct VulInstance::BuilderDetails;

    public:
        Builder() noexcept;
        ~Builder() noexcept;
        Builder &SetApplicationInfo(VkApplicationInfo const &appInfo) noexcept;
        Builder &SetRequiredExtensions(std::unordered_set<std::string> const &exts) noexcept;
        VulInstancePtr Build();
    };

    /**
     * @param instance 已创建的 VkInstance
     * @param shared   是否为共享实例（共享时不销毁）
     */
    explicit VulInstance(VkInstance instance, bool shared = false);
    ~VulInstance() override;

private:
    bool m_shared = false;
};

END_NS_BACKEND