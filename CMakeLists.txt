cmake_minimum_required(VERSION 3.20)
project(Backend VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(3rd)

file(GLOB_RECURSE BACKEND_SOURCES src/*.cpp)
add_library(Backend STATIC ${BACKEND_SOURCES})
target_include_directories(Backend PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(Backend PUBLIC ${THIRD_PARTY_LIBS})
