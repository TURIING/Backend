#pragma once
#include "Backend/DriverDefine.h"

#include <cstdint>
#include <utility>

#include "Common.h"

BEGIN_NS_BACKEND

struct HwBase {};

struct HwVertexBufferInfo : public HwBase {
    uint8_t bufferCount{};
    uint8_t attributeCount{};
    bool    padding[2]{};
    HwVertexBufferInfo() noexcept = default;
    HwVertexBufferInfo(uint8_t const bufferCount, uint8_t const attributeCount) noexcept : bufferCount(bufferCount), attributeCount(attributeCount) {}
};

struct HwVertexBuffer : public HwBase {
    uint32_t vertexCount{};
    uint8_t  bufferObjectsVersion{ 0xff };
    struct {
        uint8_t asynchronous : 1;
        uint8_t reserved : 7;
    };
    bool padding[2]{};
    HwVertexBuffer() noexcept : asynchronous{}, reserved{} {}
    explicit HwVertexBuffer(uint32_t const vertexCount, bool const async = false) noexcept
        : vertexCount(vertexCount), asynchronous(async), reserved{} {}
};

struct HwBufferObject : public HwBase {
    uint32_t byteCount : 31;
    uint32_t asynchronous : 1;

    HwBufferObject() noexcept = default;
    HwBufferObject(uint32_t byteCount, bool async) noexcept : byteCount(byteCount), asynchronous(async) {}
};

constexpr uint32_t kIndexCountBits  = 26;
constexpr uint32_t kElementSizeBits = 5;
constexpr uint8_t  kMaxElementSize  = 16;

struct HwIndexBuffer : public HwBase {
    uint32_t count : kIndexCountBits;
    uint32_t elementSize : kElementSizeBits;
    uint32_t asynchronous : 1;

    HwIndexBuffer() noexcept : count{}, elementSize{}, asynchronous{} {}
    HwIndexBuffer(uint8_t elementSize, uint32_t indexCount, bool async) noexcept
        : count(indexCount), elementSize(elementSize), asynchronous(async) {
        LOG_ASSERT(elementSize > 0 && elementSize <= kMaxElementSize);
        LOG_ASSERT(indexCount < (1u << kIndexCountBits));
    }
};

struct HwRenderTarget : public HwBase {
    uint32_t width{};
    uint32_t height{};

    HwRenderTarget() noexcept = default;
    HwRenderTarget(uint32_t const w, uint32_t const h) noexcept : width(w), height(h) {}
};

// 交换链句柄本身即标识，故只持平台层裸句柄
struct HwSwapChain : public HwBase {
    Platform::SwapChain *swapChain = nullptr;
};

struct HwProgram : public HwBase {
    NS_UTILS::String name;

    HwProgram() noexcept = default;
    explicit HwProgram(NS_UTILS::String name) noexcept : name(std::move(name)) {}
};

struct HwRenderPrimitive : public HwBase {
    PrimitiveType type = PrimitiveType::TRIANGLES;
};

struct HwDescriptorSetLayout : public HwBase {
    HwDescriptorSetLayout() noexcept = default;
};

struct HwDescriptorSet : public HwBase {
    HwDescriptorSet() noexcept = default;
};

struct HwFence : public HwBase {
    Platform::Fence *fence = nullptr;
};

struct HwSync : public HwBase {
    Platform::Sync *sync = nullptr;
};

// 计时查询句柄：实际查询对象由 VulkanQueryManager 持有
struct HwTimerQuery : public HwBase {};

// 纹理的创建参数；Vulkan 侧句柄与布局跟踪由 VulkanTexture 补充
struct HwTexture : public HwBase {
    uint32_t    width{};
    uint32_t    height{};
    uint32_t    depth{};
    SamplerType target{};
    // 至多 15 级（纹理边长上限 32768）
    uint8_t       levels : 4;
    uint8_t       samples : 4;
    TextureFormat format{};
    struct {
        uint8_t asynchronous : 1;
        uint8_t reserved : 7;
    };
    TextureUsage usage{};
    uint16_t     reserved1 = 0;

    HwTexture() noexcept : levels{}, samples{}, asynchronous(0), reserved(0) {}
    HwTexture(SamplerType const target, uint8_t const levels, uint8_t const samples, uint32_t const width, uint32_t const height,
              uint32_t const depth, TextureFormat const fmt, TextureUsage const usage, bool const async) noexcept
        : width(width),
          height(height),
          depth(depth),
          target(target),
          levels(levels),
          samples(samples),
          format(fmt),
          asynchronous(async),
          reserved(0),
          usage(usage) {}
};

// 内存映射缓冲的句柄基类：具体窗口由 VulkanMemoryMappedBuffer 承载
struct HwMemoryMappedBuffer : public HwBase {};

// 视频流句柄：流路径已按设计砍掉，只保留类型以满足驱动接口签名
struct HwStream : public HwBase {};

END_NS_BACKEND
