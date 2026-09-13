#pragma once

#include "Backend/BufferDescriptor.h"
#include "Backend/DriverDefine.h"

#include "Utils/Debug.h"
#include "Utils/Macro.h"

#include <cstddef>
#include <cstdint>

BEGIN_NS_BACKEND

/**
 * 主内存中图像数据的描述符，典型用途是把图像数据从 CPU 传到 GPU。
 *
 * PixelBufferDescriptor 拥有所引用的内存块，故不可拷贝、只可移动；析构时释放对内存块的所有权。
 *
 * 像素布局与压缩图像布局共用同一块存储，以 type 区分当前有效的那一组字段：type 为
 * COMPRESSED 时读 imageSize / compressedFormat，否则读 stride / format。
 */
class PixelBufferDescriptor : public BufferDescriptor {
public:
    using PixelDataFormat = Backend::PixelDataFormat;
    using PixelDataType   = Backend::PixelDataType;

    PixelBufferDescriptor() = default;

    PixelBufferDescriptor(void const* buffer, size_t size,
            PixelDataFormat format, PixelDataType type, uint8_t alignment,
            uint32_t left, uint32_t top, uint32_t stride,
            CallbackHandler* handler, Callback callback, void* user = nullptr) noexcept
            : BufferDescriptor(buffer, size, handler, callback, user),
              left(left), top(top), stride(stride),
              format(format), type(type), alignment(alignment) {}

    PixelBufferDescriptor(void const* buffer, size_t size,
            PixelDataFormat format, PixelDataType type, uint8_t alignment = 1,
            uint32_t left = 0, uint32_t top = 0, uint32_t stride = 0,
            Callback callback = nullptr, void* user = nullptr) noexcept
            : BufferDescriptor(buffer, size, callback, user),
              left(left), top(top), stride(stride),
              format(format), type(type), alignment(alignment) {}

    PixelBufferDescriptor(void const* buffer, size_t size,
            PixelDataFormat format, PixelDataType type,
            CallbackHandler* handler, Callback callback, void* user = nullptr) noexcept
            : BufferDescriptor(buffer, size, handler, callback, user),
              stride(0), format(format), type(type), alignment(1) {}

    PixelBufferDescriptor(void const* buffer, size_t size,
            PixelDataFormat format, PixelDataType type,
            Callback callback, void* user = nullptr) noexcept
            : BufferDescriptor(buffer, size, callback, user),
              stride(0), format(format), type(type), alignment(1) {}

    PixelBufferDescriptor(void const* buffer, size_t size,
            Backend::CompressedPixelDataType format, uint32_t imageSize,
            CallbackHandler* handler, Callback callback, void* user = nullptr) noexcept
            : BufferDescriptor(buffer, size, handler, callback, user),
              imageSize(imageSize), compressedFormat(format), type(PixelDataType::COMPRESSED),
              alignment(1) {}

    PixelBufferDescriptor(void const* buffer, size_t size,
            Backend::CompressedPixelDataType format, uint32_t imageSize,
            Callback callback, void* user = nullptr) noexcept
            : BufferDescriptor(buffer, size, callback, user),
              imageSize(imageSize), compressedFormat(format), type(PixelDataType::COMPRESSED),
              alignment(1) {}

    // 单像素字节数；压缩格式的像素大小由格式本身描述，此处返回 0
    static constexpr size_t ComputePixelSize(PixelDataFormat format, PixelDataType type) noexcept {
        if (type == PixelDataType::COMPRESSED) {
            return 0;
        }

        size_t n = 0;
        switch (format) {
            case PixelDataFormat::R:
            case PixelDataFormat::R_INTEGER:
            case PixelDataFormat::DEPTH_COMPONENT:
            case PixelDataFormat::ALPHA:
                n = 1;
                break;
            case PixelDataFormat::RG:
            case PixelDataFormat::RG_INTEGER:
            case PixelDataFormat::DEPTH_STENCIL:
                n = 2;
                break;
            case PixelDataFormat::RGB:
            case PixelDataFormat::RGB_INTEGER:
                n = 3;
                break;
            case PixelDataFormat::UNUSED:  // 曾为 rgbm，不应出现
            case PixelDataFormat::RGBA:
            case PixelDataFormat::RGBA_INTEGER:
                n = 4;
                break;
        }

        size_t bpp = n;
        switch (type) {
            case PixelDataType::COMPRESSED:  // 不可达，仅为消除编译器的穷举告警
            case PixelDataType::UBYTE:
            case PixelDataType::BYTE:
                break;
            case PixelDataType::USHORT:
            case PixelDataType::SHORT:
            case PixelDataType::HALF:
                bpp *= 2;
                break;
            case PixelDataType::UINT:
            case PixelDataType::INT:
            case PixelDataType::FLOAT:
                bpp *= 4;
                break;
            case PixelDataType::UINT_10F_11F_11F_REV:
                // 每像素固定 4 字节，与 format 无关
                assert_invariant(format == PixelDataFormat::RGB);
                bpp = 4;
                break;
            case PixelDataType::UINT_2_10_10_10_REV:
                // 每像素固定 4 字节，与 format 无关
                assert_invariant(format == PixelDataFormat::RGBA);
                bpp = 4;
                break;
            case PixelDataType::USHORT_565:
                // 每像素固定 2 字节，与 format 无关
                assert_invariant(format == PixelDataFormat::RGB);
                bpp = 2;
                break;
        }
        return bpp;
    }

    // 容纳一行像素所需的字节数；行按 alignment 向上对齐
    static constexpr size_t ComputeDataSize(PixelDataFormat format, PixelDataType type,
            size_t stride, size_t height, size_t alignment) noexcept {
        assert_invariant(alignment);

        size_t const bpp        = ComputePixelSize(format, type);
        size_t const bpr        = bpp * stride;
        size_t const bprAligned = (bpr + (alignment - 1)) & (~alignment + 1);
        return bprAligned * height;
    }

    //! 左上角 x 坐标（像素）
    uint32_t left = 0;

    //! 左上角 y 坐标（像素）
    uint32_t top = 0;

    union {
        struct {
            //! 一行像素的字节数
            uint32_t stride;
            //! 像素数据格式
            PixelDataFormat format;
        };
        struct {
            //! 压缩图像的压缩后字节数
            uint32_t imageSize;
            //! 压缩图像格式
            Backend::CompressedPixelDataType compressedFormat;
        };
    };

    //! 像素数据类型；位宽与上游一致
    PixelDataType type : 4;

    //! 行对齐字节数；位宽与上游一致
    uint8_t alignment : 4;
};

END_NS_BACKEND
