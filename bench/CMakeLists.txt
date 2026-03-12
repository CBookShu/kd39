add_executable(gateway_http_ws_bench
    gateway_http_ws_bench.cpp
)

target_link_libraries(gateway_http_ws_bench
    PRIVATE
        compiler_options
        common_base
        api_proto
        storage_mysql
        storage_redis
        user_service_lib
        access_gateway_lib
        gRPC::grpc++
)
