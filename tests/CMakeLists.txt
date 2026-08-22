# Backend 单元测试构建配置
# 复用 Utils 提供的 gtest：同一 CMake 工程内 target 名全局唯一，Backend 不重复 add_subdirectory

# 目标名称统一用变量，避免硬编码（见 .claude/rules/code-style.md）
set(BACKEND_TESTS_NAME BackendTests)

file(GLOB_RECURSE TEST_SOURCES *.cpp)
add_executable(${BACKEND_TESTS_NAME} ${TEST_SOURCES})
find_package(Threads REQUIRED)
target_include_directories(${BACKEND_TESTS_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(${BACKEND_TESTS_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/3rd/Utils/include)
target_link_libraries(${BACKEND_TESTS_NAME} PRIVATE ${BACKEND_NAME} Utils gtest_main Threads::Threads)
add_test(NAME ${BACKEND_TESTS_NAME} COMMAND ${BACKEND_TESTS_NAME})
