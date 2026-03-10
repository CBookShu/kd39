add_library(coord_registry STATIC
    registry/service_registry.cpp
)

target_include_directories(coord_registry
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(coord_registry
    PUBLIC compiler_options common_base
)

add_library(coord_lock STATIC
    lock/distributed_lock.cpp
)

target_include_directories(coord_lock
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(coord_lock
    PUBLIC compiler_options common_base
)

add_library(coord_election STATIC
    election/leader_election.cpp
)

target_include_directories(coord_election
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(coord_election
    PUBLIC compiler_options common_base
)

add_library(coord_watch STATIC
    watch/config_watcher.cpp
)

target_include_directories(coord_watch
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(coord_watch
    PUBLIC compiler_options common_base
)

add_subdirectory(impl/etcd)
