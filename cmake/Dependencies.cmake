find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

# Core dependencies are declared in `vcpkg.json` and resolved through
# `find_package(... CONFIG)` so vcpkg manifest mode can supply them uniformly on
# Windows and Linux.
find_package(yaml-cpp CONFIG QUIET)
find_package(Boost CONFIG QUIET COMPONENTS system)
find_package(boost_cobalt CONFIG QUIET)

if(KD39_ENABLE_TESTS)
    find_package(GTest CONFIG QUIET)
endif()

if(KD39_ENABLE_ETCD)
    # etcd-cpp-apiv3 is intentionally not taken from vcpkg in this repository.
    # We keep the main dependency graph on vcpkg, but fetch the etcd client
    # source directly via CMake so we can pin an upstream tag independently of
    # the active vcpkg baseline.
    include(FetchEtcdCppApiv3)
    kd39_fetch_etcd_cpp_apiv3()
endif()

if(KD39_ENABLE_OBSERVABILITY)
    find_package(opentelemetry-cpp CONFIG QUIET)
    find_package(prometheus-cpp CONFIG QUIET)
endif()

if(KD39_ENABLE_GATEWAY)
    # Future full Boost.Beast/Asio networking enablement can use these packages.
    find_package(boost-asio CONFIG QUIET)
    find_package(boost-beast CONFIG QUIET)
endif()

set(_protoc_hints "")
set(_grpc_plugin_hints "")
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_HOST_TRIPLET)
    list(APPEND _protoc_hints
        "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/protobuf"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools")
    list(APPEND _grpc_plugin_hints
        "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/grpc"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools")
endif()

# In manifest mode, `protobuf` and `grpc` are also declared as host
# dependencies so `protoc` and `grpc_cpp_plugin` come from the build machine.
if(TARGET protobuf::protoc)
    set(PROTOC_EXECUTABLE "$<TARGET_FILE:protobuf::protoc>")
elseif(DEFINED Protobuf_PROTOC_EXECUTABLE)
    set(PROTOC_EXECUTABLE "${Protobuf_PROTOC_EXECUTABLE}")
else()
    find_program(PROTOC_EXECUTABLE protoc HINTS ${_protoc_hints})
endif()

if(TARGET gRPC::grpc_cpp_plugin)
    set(GRPC_CPP_PLUGIN_EXECUTABLE "$<TARGET_FILE:gRPC::grpc_cpp_plugin>")
else()
    find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin HINTS ${_grpc_plugin_hints})
endif()

if(NOT PROTOC_EXECUTABLE)
    message(WARNING "protoc not found – proto code generation will fail")
endif()

if(NOT GRPC_CPP_PLUGIN_EXECUTABLE)
    message(WARNING "grpc_cpp_plugin not found – proto code generation will fail")
endif()
