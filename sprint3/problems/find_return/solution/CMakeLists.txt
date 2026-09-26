cmake_minimum_required(VERSION 3.11)

project(game_server CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

find_package(Threads REQUIRED)

add_executable(game_server
    src/main.cpp
    src/http_server.cpp
    src/http_server.h
    src/json_loader.cpp
    src/json_loader.h
    src/model.cpp
    src/model.h
    src/request_handler.cpp
    src/request_handler.h
    src/sdk.h
    src/tagged.h
    src/boost_json.cpp
    src/collision_detector.cpp
    src/collision_detector.h
    src/extra_data.h
    src/geom.h
    src/loot_generator.cpp
    src/loot_generator.h
)
target_compile_definitions(game_server PRIVATE COMMAND_LINE_MODE)
target_link_libraries(game_server PRIVATE CONAN_PKG::boost Threads::Threads)
