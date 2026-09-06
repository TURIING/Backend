set(VMA_ROOT ${CMAKE_CURRENT_LIST_DIR}/VulkanMemoryAllocator)
set(VMA_TARGET_NAME vma)

add_library(${VMA_TARGET_NAME} INTERFACE)
add_library(${VMA_TARGET_NAME}::${VMA_TARGET_NAME} ALIAS ${VMA_TARGET_NAME})

find_package(Vulkan REQUIRED)

target_include_directories(${VMA_TARGET_NAME} SYSTEM INTERFACE ${VMA_ROOT}/include ${Vulkan_INCLUDE_DIRS})
# VMA 与 volk 动态加载配合：不持静态函数指针，运行期经 VmaVulkanFunctions 解析
target_compile_definitions(${VMA_TARGET_NAME} INTERFACE VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
