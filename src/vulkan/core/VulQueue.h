#pragma once

#include "Defines.h"
#include "VulLogicDevice.h"
#include "VulObject.h"

BEGIN_NS_BACKEND

class VulQueue;
DECLARE_SHARE_PTR_CLASS(VulQueue);

// VkQueue 封装：队列由逻辑设备管理，不销毁
class VulQueue final : public VulObject<VkQueue> {
    struct BuilderDetails;

public:
    class Builder : public NS_UTILS::BuilderBase<BuilderDetails> {
        friend struct BuilderDetails;

    public:
        Builder() noexcept;
        ~Builder() noexcept;
        Builder &SetDevice(VulLogicDevicePtr device) noexcept;
        Builder &SetQueueFamilyIndex(uint32_t index) noexcept;
        Builder &SetQueueIndex(uint32_t index) noexcept;
        Builder &SetProtected(bool enabled) noexcept;
        VulQueuePtr Build();
    };

    explicit VulQueue(VkQueue queue);
};

END_NS_BACKEND