add_library(access_gateway_lib STATIC
    http/http_server.cpp
    ws/ws_server.cpp
    auth/auth_middleware.cpp
    routing/grpc_router.cpp
    context/gateway_context.cpp
)

target_include_directories(access_gateway_lib
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(access_gateway_lib
    PUBLIC
        compiler_options
        common_base
        framework_app
        framework_rpc
        framework_governance
        api_proto
        coord_registry
        coord_etcd
        observability_core
)

if(TARGET Boost::system)
    target_link_libraries(access_gateway_lib PUBLIC Boost::system)
endif()

add_executable(access_gateway
    main.cpp
)

target_link_libraries(access_gateway
    PRIVATE access_gateway_lib
)
