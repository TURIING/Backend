# Backend 单元测试构建配置

set(BACKEND_TESTS_NAME BackendTests)

file(GLOB_RECURSE TEST_SOURCES *.cpp)
add_executable(${BACKEND_TESTS_NAME} ${TEST_SOURCES})
target_include_directories(${BACKEND_TESTS_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(${BACKEND_TESTS_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/3rd/Utils/include)
target_link_libraries(${BACKEND_TESTS_NAME} PRIVATE ${BACKEND_NAME} Utils)