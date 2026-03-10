add_library(framework_app STATIC
    app/application.cpp
)

target_include_directories(framework_app
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(framework_app
    PUBLIC
        compiler_options
        common_base
        spdlog::spdlog
)

add_library(framework_rpc STATIC
    rpc/client_interceptor.cpp
    rpc/server_interceptor.cpp
)

target_include_directories(framework_rpc
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(framework_rpc
    PUBLIC
        compiler_options
        common_base
        gRPC::grpc++
)

add_subdirectory(governance)
