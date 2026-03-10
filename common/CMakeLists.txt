add_library(common_base STATIC
    context/request_context.cpp
    context/context_store.cpp
    config/app_config.cpp
    error/error_code.cpp
    log/logger.cpp
)

target_include_directories(common_base
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(common_base
    PUBLIC
        compiler_options
        spdlog::spdlog
        fmt::fmt
        nlohmann_json::nlohmann_json
)
