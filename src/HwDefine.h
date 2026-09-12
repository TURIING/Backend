#pragma once
#include "Backend/DriverDefine.h"

#include <cstdint>

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

END_NS_BACKEND