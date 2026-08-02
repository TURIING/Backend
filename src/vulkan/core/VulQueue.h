#pragma once

#include "Defines.h"
#include "VulLogicDevice.h"
#include "VulObject.h"

BEGIN_NS_BACKEND

class VulQueue;
DECLARE_SHARE_PTR_CLASS(VulQueue);  // 前置声明 Ptr（Builder::Build 返回类型需要）

// VkQueue 封装：队列由逻辑设备管理，不销毁
class VulQueue final : public VulObject<VkQueue> {
    struct BuilderDetails;

public:
    /**
     * 创建信息（Builder 模式）
     */
    class Builder : public BuilderBase<BuilderDetails> {
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

    /**
     * @param queue 已获取的 VkQueue
     */
    explicit VulQueue(VkQueue queue);
};

END_NS_BACKEND