set(VOLK_ROOT ${CMAKE_CURRENT_LIST_DIR}/volk)

# volk 目标名称统一用变量，避免硬编码（见 .claude/rules/code-style.md）
set(VOLK_TARGET_NAME volk)

if(APPLE)
    set(VOLK_PLATFORM_DEFINES
        VK_USE_PLATFORM_MACOS_MVK   
        VK_USE_PLATFORM_METAL_EXT   
    )
elseif(WIN32)
    set(VOLK_PLATFORM_DEFINES VK_USE_PLATFORM_WIN32_KHR)
elseif(UNIX)
    set(VOLK_PLATFORM_DEFINES
        VK_USE_PLATFORM_XCB_KHR
        VK_USE_PLATFORM_XLIB_KHR
        VK_USE_PLATFORM_WAYLAND_KHR
    )
endif()

find_package(Vulkan REQUIRED)

enable_language(C)

add_library(${VOLK_TARGET_NAME} STATIC ${VOLK_ROOT}/volk.c ${VOLK_ROOT}/volk.h)
add_library(${VOLK_TARGET_NAME}::${VOLK_TARGET_NAME} ALIAS ${VOLK_TARGET_NAME})

target_include_directories(${VOLK_TARGET_NAME} PUBLIC ${VOLK_ROOT})
target_include_directories(${VOLK_TARGET_NAME} SYSTEM PUBLIC ${Vulkan_INCLUDE_DIRS})

target_compile_definitions(${VOLK_TARGET_NAME} PUBLIC VK_NO_PROTOTYPES ${VOLK_PLATFORM_DEFINES})

if(NOT WIN32)
    target_link_libraries(${VOLK_TARGET_NAME} PUBLIC ${CMAKE_DL_LIBS})
endif()
