add_executable(grpc_cli
    main.cpp
)

target_link_libraries(grpc_cli
    PRIVATE
        compiler_options
        common_base
        framework_rpc
        api_proto
)
