cmake_minimum_required(VERSION 3.14)
project(TinyWebServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)
find_package(yaml-cpp REQUIRED)
find_path(REDIS_PLUS_PLUS_INCLUDE_DIR sw/redis++/redis++.h)
find_library(REDIS_PLUS_PLUS_LIBRARY NAMES redis++)
find_library(HIREDIS_LIBRARY NAMES hiredis)
find_path(RDKAFKA_INCLUDE_DIR librdkafka/rdkafka.h)
find_library(RDKAFKA_LIBRARY NAMES rdkafka)

include_directories(
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/third_party
)

set(SOURCES
    main.cpp
    webserver.cpp
    analytics/click_event_producer.cpp
    config/config.cpp
    log/log.cpp
    CGImysql/sql_connection_pool.cpp
    timer/lst_timer.cpp
    http/http_conn.cpp
    http/router.cpp
    http/response.cpp
    handler/health_handler.cpp
    handler/short_url_handler.cpp
    shorturl/base62.cpp
    shorturl/bloom_filter.cpp
    shorturl/singleflight.cpp
    shorturl/shard_router.cpp
    shorturl/snowflake.cpp
    shorturl/short_url_cache.cpp
    shorturl/short_url_repository.cpp
)

add_executable(server ${SOURCES})

if (REDIS_PLUS_PLUS_INCLUDE_DIR AND REDIS_PLUS_PLUS_LIBRARY AND HIREDIS_LIBRARY)
    target_compile_definitions(server PRIVATE HAVE_REDIS_PLUS_PLUS)
    target_include_directories(server PRIVATE ${REDIS_PLUS_PLUS_INCLUDE_DIR})
    target_link_libraries(server
        ${REDIS_PLUS_PLUS_LIBRARY}
        ${HIREDIS_LIBRARY}
    )
else()
    message(WARNING "redis-plus-plus or hiredis not found; Redis cache layer will build in DB-only fallback mode")
endif()

if (RDKAFKA_INCLUDE_DIR AND RDKAFKA_LIBRARY)
    target_compile_definitions(server PRIVATE HAVE_RDKAFKA)
    target_include_directories(server PRIVATE ${RDKAFKA_INCLUDE_DIR})
    target_link_libraries(server ${RDKAFKA_LIBRARY})
else()
    message(WARNING "librdkafka not found; Kafka click producer will build in no-op fallback mode")
endif()

target_link_libraries(server
    Threads::Threads
    mysqlclient
    yaml-cpp
)
