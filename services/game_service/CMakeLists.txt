add_library(game_service_lib STATIC
    game_service_impl.cpp
)

target_include_directories(game_service_lib
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(game_service_lib
    PUBLIC
        compiler_options
        common_base
        framework_rpc
        api_proto
        storage_redis
        mq_core
        coord_registry
)

add_executable(game_service
    main.cpp
)

target_include_directories(game_service
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(game_service
    PRIVATE
        framework_app
        game_service_lib
        coord_etcd
)
