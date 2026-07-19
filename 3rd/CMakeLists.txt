add_subdirectory(Utils)
add_subdirectory(volk)
set(THIRD_PARTY_LIBS
    Utils
    volk
    CACHE INTERNAL "Third-party library targets"
)
