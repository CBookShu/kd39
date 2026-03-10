add_library(mq_core STATIC
    core/producer.cpp
    core/consumer.cpp
)

target_include_directories(mq_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(mq_core
    PUBLIC compiler_options common_base
)
