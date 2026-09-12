#pragma once

#include "Backend/DriverDefine.h"

#include "vk_mem_alloc.h"
#include "vulkan/resource/Resource.h"

#include <cstdint>
#include <functional>

BEGIN_NS_BACKEND

enum class VulkanBufferBinding : uint8_t {
    Unknown,
    Vertex,
    Index,
    Uniform,
    ShaderStorage,
};

struct VulkanGpuBuffer {
    VkBuffer            vkbuffer       = VK_NULL_HANDLE;
    VmaAllocation       vmaAllocation  = VK_NULL_HANDLE;
    VmaAllocationInfo   allocationInfo = {};
    uint32_t            numBytes       = 0;
    VulkanBufferBinding binding        = VulkanBufferBinding::Unknown;
};

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

END_NS_BACKEND
