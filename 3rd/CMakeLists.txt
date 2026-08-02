include(volk.cmake)
add_subdirectory(Utils)
set(THIRD_PARTY_LIBS
    Utils
    volk
    CACHE INTERNAL "Third-party library targets"
)
