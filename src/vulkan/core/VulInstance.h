#pragma once

#include <functional>
#include <unordered_set>

#include "Defines.h"
#include "VulObject.h"

BEGIN_NS_BACKEND

class VulInstance;
DECLARE_SHARE_PTR_CLASS(VulInstance);

class VulInstance final : public VulObject<VkInstance> {
    struct BuilderDetails;

public:
    using InstanceCreator = std::function<VkInstance(VkInstanceCreateInfo const &)>;

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