#include "Backend/Driver.h"

#include "Utils/Macro.h"

BEGIN_NS_BACKEND

size_t Driver::GetElementTypeSize(ElementType type) noexcept {
    switch (type) {
        CASE_FROM_TO(ElementType::BYTE, 1);
        CASE_FROM_TO(ElementType::BYTE2, 2);
        CASE_FROM_TO(ElementType::BYTE3, 3);
        CASE_FROM_TO(ElementType::BYTE4, 4);
        CASE_FROM_TO(ElementType::UBYTE, 1);
        CASE_FROM_TO(ElementType::UBYTE2, 2);
        CASE_FROM_TO(ElementType::UBYTE3, 3);
        CASE_FROM_TO(ElementType::UBYTE4, 4);
        CASE_FROM_TO(ElementType::SHORT, 2);
        CASE_FROM_TO(ElementType::SHORT2, 4);
        CASE_FROM_TO(ElementType::SHORT3, 6);
        CASE_FROM_TO(ElementType::SHORT4, 8);
        CASE_FROM_TO(ElementType::USHORT, 2);
        CASE_FROM_TO(ElementType::USHORT2, 4);
        CASE_FROM_TO(ElementType::USHORT3, 6);
        CASE_FROM_TO(ElementType::USHORT4, 8);
        CASE_FROM_TO(ElementType::INT, 4);
        CASE_FROM_TO(ElementType::UINT, 4);
        CASE_FROM_TO(ElementType::FLOAT, 4);
        CASE_FROM_TO(ElementType::FLOAT2, 8);
        CASE_FROM_TO(ElementType::FLOAT3, 12);
        CASE_FROM_TO(ElementType::FLOAT4, 16);
        CASE_FROM_TO(ElementType::HALF, 2);
        CASE_FROM_TO(ElementType::HALF2, 4);
        CASE_FROM_TO(ElementType::HALF3, 6);
        CASE_FROM_TO(ElementType::HALF4, 8);
    }
    return 0;
}

END_NS_BACKEND
