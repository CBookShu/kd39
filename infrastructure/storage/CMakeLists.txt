add_library(storage_mysql STATIC
    mysql/mysql_connection_pool.cpp
)

target_include_directories(storage_mysql
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(storage_mysql
    PUBLIC compiler_options common_base
)

add_library(storage_redis STATIC
    redis/redis_client.cpp
)

target_include_directories(storage_redis
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(storage_redis
    PUBLIC compiler_options common_base
)
