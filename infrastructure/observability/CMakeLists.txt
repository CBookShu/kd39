add_library(observability_core STATIC
    telemetry.cpp
    metrics.cpp
)

target_include_directories(observability_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(observability_core
    PUBLIC
        compiler_options
        common_base
)
