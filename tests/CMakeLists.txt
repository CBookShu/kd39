if(NOT GTest_FOUND)
    find_package(GTest CONFIG QUIET)
endif()

if(GTest_FOUND)
    add_executable(unit_tests
        common/error_code_test.cpp
        common/request_context_test.cpp
    )

    target_link_libraries(unit_tests
        PRIVATE
            common_base
            GTest::gtest
            GTest::gtest_main
    )

    add_executable(integration_tests
        integration/config_service_test.cpp
        integration/user_service_test.cpp
        integration/gateway_test.cpp
        integration/mq_test.cpp
    )

    target_link_libraries(integration_tests
        PRIVATE
            common_base
            api_proto
            storage_mysql
            storage_redis
            coord_etcd
            mq_core
            config_service_lib
            user_service_lib
            game_service_lib
            access_gateway_lib
            GTest::gtest
            GTest::gtest_main
            gRPC::grpc++
    )

    include(GoogleTest)
    gtest_discover_tests(unit_tests)
    gtest_discover_tests(integration_tests)
else()
    message(STATUS "GTest not found – skipping tests")
endif()
