include(glfw.cmake)
include(vma.cmake)
include(volk.cmake)

add_subdirectory(Utils)

set(THIRD_PARTY_LIBS
    Utils
    ${GLFW_TARGET_NAME}
    ${VMA_TARGET_NAME}
    ${VOLK_TARGET_NAME}
    CACHE INTERNAL "Third-party library targets"
)
