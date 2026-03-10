add_library(coord_etcd STATIC
    etcd_registry.cpp
    etcd_lock.cpp
    etcd_election.cpp
    etcd_config_provider.cpp
)

target_include_directories(coord_etcd
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(coord_etcd
    PUBLIC
        compiler_options
        common_base
        coord_registry
        coord_lock
        coord_election
        coord_watch
)
