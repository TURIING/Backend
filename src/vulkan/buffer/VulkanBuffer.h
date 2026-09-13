#pragma once

#include "Backend/DriverDefine.h"

#include "vulkan/VulkanMemory.h"
#include "vulkan/resource/Resource.h"

#include <cstdint>
#include <functional>

BEGIN_NS_BACKEND

class VulkanBuffer : public Resource {
public:
    using OnRecycle = std::function<void(VulkanGpuBuffer const*)>;

    VulkanBuffer(VulkanGpuBuffer const* gpuBuffer, OnRecycle&& onRecycleFn)
        : m_gpuBuffer(gpuBuffer), m_onRecycleFn(onRecycleFn) {}

    ~VulkanBuffer() {
        if (m_onRecycleFn) {
            m_onRecycleFn(m_gpuBuffer);
        }
    }

    NODISCARD VulkanGpuBuffer const* GetGpuBuffer() const { return m_gpuBuffer; }

private:
    VulkanGpuBuffer const* m_gpuBuffer;
    OnRecycle              m_onRecycleFn;
};
DECLARE_SHARE_PTR_CLASS(VulkanBuffer);

END_NS_BACKEND
