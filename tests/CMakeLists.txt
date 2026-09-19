# Backend 单元测试构建配置

set(BACKEND_TESTS_NAME BackendTests)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS *.cpp *.mm)
add_executable(${BACKEND_TESTS_NAME} ${TEST_SOURCES})
target_include_directories(${BACKEND_TESTS_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(${BACKEND_TESTS_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/3rd/Utils/include)
target_link_libraries(${BACKEND_TESTS_NAME} PRIVATE ${BACKEND_NAME} Utils ${GLFW_TARGET_NAME})

# CAMetalLayer 由 QuartzCore 提供，NSWindow/NSView 由 Cocoa 提供；GLFW 只链了后者
if(APPLE)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(COCOA_FRAMEWORK     Cocoa     REQUIRED)
    target_link_libraries(${BACKEND_TESTS_NAME} PRIVATE ${QUARTZCORE_FRAMEWORK} ${COCOA_FRAMEWORK})
endif()
