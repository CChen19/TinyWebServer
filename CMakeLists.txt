cmake_minimum_required(VERSION 3.14)
project(TinyWebServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)
find_package(yaml-cpp REQUIRED)

include_directories(
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/third_party
)

set(SOURCES
    main.cpp
    webserver.cpp
    config/config.cpp
    log/log.cpp
    CGImysql/sql_connection_pool.cpp
    timer/lst_timer.cpp
    http/http_conn.cpp
    http/router.cpp
    http/response.cpp
    handler/health_handler.cpp
)

add_executable(server ${SOURCES})

target_link_libraries(server
    Threads::Threads
    mysqlclient
    yaml-cpp
)
