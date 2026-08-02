set(VOLK_ROOT ${CMAKE_CURRENT_LIST_DIR}/volk)

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

add_library(volk STATIC ${VOLK_ROOT}/volk.c ${VOLK_ROOT}/volk.h)
add_library(volk::volk ALIAS volk)

target_include_directories(volk PUBLIC ${VOLK_ROOT})
target_include_directories(volk SYSTEM PUBLIC ${Vulkan_INCLUDE_DIRS})

target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES ${VOLK_PLATFORM_DEFINES})

if(NOT WIN32)
    target_link_libraries(volk PUBLIC ${CMAKE_DL_LIBS})
endif()
