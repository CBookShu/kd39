add_library(framework_governance STATIC
    rate_limiter.cpp
    circuit_breaker.cpp
    retry_policy.cpp
    traffic_router.cpp
)

target_include_directories(framework_governance
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(framework_governance
    PUBLIC
        compiler_options
        common_base
        coord_registry
)
