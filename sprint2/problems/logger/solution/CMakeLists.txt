cmake_minimum_required(VERSION 3.9)

project(HelloLog CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

add_executable(hello_log main.cpp)
target_include_directories(hello_log PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
