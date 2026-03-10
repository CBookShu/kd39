add_library(config_service_lib STATIC
    config_service_impl.cpp
)

target_include_directories(config_service_lib
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(config_service_lib
    PUBLIC
        compiler_options
        common_base
        framework_rpc
        api_proto
        coord_watch
        coord_etcd
        storage_mysql
        storage_redis
        mq_core
)

add_executable(config_service
    main.cpp
)

target_include_directories(config_service
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(config_service
    PRIVATE
        framework_app
        config_service_lib
)
