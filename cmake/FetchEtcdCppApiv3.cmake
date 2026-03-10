include(FetchContent)

function(kd39_fetch_etcd_cpp_apiv3)
    if(TARGET etcd-cpp-api)
        return()
    endif()

    # Keep the first integration lightweight: synchronous runtime only,
    # no upstream tests, and no extra warning strictness bleeding into
    # the parent build.
    set(BUILD_ETCD_CORE_ONLY "${KD39_ETCD_CORE_ONLY}" CACHE BOOL
        "Build etcd-cpp-apiv3 synchronous runtime only" FORCE)
    set(BUILD_ETCD_TESTS OFF CACHE BOOL
        "Disable upstream etcd-cpp-apiv3 tests in kd39" FORCE)
    set(BUILD_WITH_NO_EXCEPTIONS OFF CACHE BOOL
        "Keep exception support for etcd-cpp-apiv3" FORCE)
    set(ETCD_W_STRICT OFF CACHE BOOL
        "Do not enable -Werror in fetched etcd-cpp-apiv3" FORCE)
    set(ETCD_CMAKE_CXX_STANDARD "${CMAKE_CXX_STANDARD}" CACHE STRING
        "Align etcd-cpp-apiv3 with the parent C++ standard" FORCE)

    message(STATUS "Fetching etcd-cpp-apiv3 ${KD39_ETCD_CPP_APIV3_TAG}")

    FetchContent_Declare(
        etcd_cpp_apiv3
        GIT_REPOSITORY https://github.com/etcd-cpp-apiv3/etcd-cpp-apiv3.git
        GIT_TAG        ${KD39_ETCD_CPP_APIV3_TAG}
        GIT_SHALLOW    TRUE
    )

    FetchContent_GetProperties(etcd_cpp_apiv3)
    if(NOT etcd_cpp_apiv3_POPULATED)
        FetchContent_Populate(etcd_cpp_apiv3)

        set(_action_cpp "${etcd_cpp_apiv3_SOURCE_DIR}/src/v3/Action.cpp")
        file(READ "${_action_cpp}" _action_cpp_contents)
        if(_action_cpp_contents MATCHES "GPR_ASSERT\\(got_tag == \\(void\\*\\) this\\);" AND
           NOT _action_cpp_contents MATCHES "#ifndef GPR_ASSERT")
            string(REPLACE
                "#include \"etcd/v3/action_constants.hpp\""
                "#include \"etcd/v3/action_constants.hpp\"\n#include <cassert>\n#ifndef GPR_ASSERT\n#define GPR_ASSERT(x) assert(x)\n#endif"
                _action_cpp_contents
                "${_action_cpp_contents}")
            file(WRITE "${_action_cpp}" "${_action_cpp_contents}")
        endif()

        add_subdirectory("${etcd_cpp_apiv3_SOURCE_DIR}" "${etcd_cpp_apiv3_BINARY_DIR}" EXCLUDE_FROM_ALL)
    endif()

    if(NOT TARGET etcd-cpp-api)
        message(FATAL_ERROR "etcd-cpp-apiv3 was fetched but target 'etcd-cpp-api' is missing")
    endif()
endfunction()
