cmake_minimum_required(VERSION 3.20)

# 项目/目标名称统一用变量，避免硬编码（见 .claude/rules/code-style.md）
set(BACKEND_NAME Backend)

project(${BACKEND_NAME} VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
set(BACKEND_MACRO_DEFINE BACKEND_DEBUG_FLAG)

enable_testing()
add_subdirectory(tests)
add_subdirectory(3rd)

file(GLOB_RECURSE BACKEND_SOURCES src/*.cpp)
add_library(${BACKEND_NAME} STATIC ${BACKEND_SOURCES})
target_include_directories(${BACKEND_NAME} PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_include_directories(${BACKEND_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(${BACKEND_NAME} PUBLIC ${THIRD_PARTY_LIBS})
target_compile_definitions(${BACKEND_NAME} PUBLIC ${BACKEND_MACRO_DEFINE})

