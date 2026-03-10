add_library(user_service_lib STATIC
    user_service_impl.cpp
)

target_include_directories(user_service_lib
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(user_service_lib
    PUBLIC
        compiler_options
        common_base
        framework_rpc
        api_proto
        storage_mysql
        storage_redis
        mq_core
        coord_registry
)

add_executable(user_service
    main.cpp
)

target_include_directories(user_service
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(user_service
    PRIVATE
        framework_app
        user_service_lib
        coord_etcd
)
