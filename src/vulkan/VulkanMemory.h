#pragma once

#include "Backend/DriverDefine.h"

#include "vk_mem_alloc.h"

#include <cstdint>

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

END_NS_BACKEND
