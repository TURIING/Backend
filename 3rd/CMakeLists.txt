include(volk.cmake)
add_subdirectory(Utils)
set(THIRD_PARTY_LIBS
    Utils
    ${VOLK_TARGET_NAME}
    CACHE INTERNAL "Third-party library targets"
)
