cmake_minimum_required(VERSION 3.11)

project(game_server CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

find_package(Threads REQUIRED)

add_executable(game_server
    src/main.cpp
    src/sdk.h
    src/tagged.h
    src/geom.h
    src/boost_json.cpp
    src/http_server.cpp
    src/http_server.h
    src/server_logging.cpp
    src/server_logging.h
    src/api_endpoints.h
    src/request_handler.cpp
    src/request_handler.h
    src/json_loader.cpp
    src/json_loader.h
    src/extra_data.h
    src/model.cpp
    src/model.h
    src/model_serialization.h
    src/collision_detector.cpp
    src/collision_detector.h
    src/loot_generator.cpp
    src/loot_generator.h
    src/state_storage.cpp
    src/state_storage.h
    src/postgres.cpp
    src/postgres.h
    src/application.cpp
    src/application.h
)
target_compile_definitions(game_server PRIVATE SERVER_LOGGING)
target_link_libraries(game_server PRIVATE CONAN_PKG::boost CONAN_PKG::libpqxx Threads::Threads)
