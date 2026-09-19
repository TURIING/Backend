#pragma once

#include "Backend/Handle.h"

#include <cstddef>
#include <cstdint>
#include <utility>

BEGIN_NS_BACKEND

struct TargetBufferInfo {
    // 参数顺序与成员声明顺序不同，故不合并为默认参数
    TargetBufferInfo(Handle<HwTexture> handle, uint8_t const level, uint16_t const layer) noexcept
            : handle(std::move(handle)), level(level), layer(layer) {}

    TargetBufferInfo(Handle<HwTexture> handle, uint8_t const level) noexcept
            : handle(handle), level(level) {}

    TargetBufferInfo(Handle<HwTexture> handle) noexcept  // NOLINT(*-explicit-constructor)
            : handle(handle) {}

    TargetBufferInfo() noexcept = default;

    Handle<HwTexture> handle;  // ! 作为渲染目标使用的纹理

    uint8_t level = 0;         // ! mipmap 层级

    //! cubemap 时为面（面到层的映射见 TextureCubemapFace）；2D 数组 / cubemap 数组 / 3D
    //! 纹理时为单层索引；multiview 纹理（layerCount > 1）时为当前 2D 数组纹理的起始层
    uint16_t layer = 0;
};

class MRT {
public:
    static constexpr uint8_t MIN_SUPPORTED_RENDER_TARGET_COUNT = 4u;

    static constexpr uint8_t MAX_SUPPORTED_RENDER_TARGET_COUNT = 8u;

    MRT() noexcept = default;

    MRT(TargetBufferInfo const& color) noexcept  // NOLINT(*-explicit-constructor)
            : mInfos{ color } {}

    MRT(TargetBufferInfo const& color0, TargetBufferInfo const& color1) noexcept
            : mInfos{ color0, color1 } {}

    MRT(TargetBufferInfo const& color0, TargetBufferInfo const& color1,
        TargetBufferInfo const& color2) noexcept
            : mInfos{ color0, color1, color2 } {}

    MRT(TargetBufferInfo const& color0, TargetBufferInfo const& color1,
        TargetBufferInfo const& color2, TargetBufferInfo const& color3) noexcept
            : mInfos{ color0, color1, color2, color3 } {}

    // 上游历史构造形式，保留以兼容既有调用点
    MRT(Handle<HwTexture> handle, uint8_t level, uint16_t layer) noexcept
            : mInfos{{ handle, level, layer }} {}

    TargetBufferInfo const& operator[](size_t const i) const noexcept { return mInfos[i]; }

    TargetBufferInfo& operator[](size_t const i) noexcept { return mInfos[i]; }

private:
    TargetBufferInfo mInfos[MAX_SUPPORTED_RENDER_TARGET_COUNT];
};

END_NS_BACKEND
