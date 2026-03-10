# ProtoGen.cmake - compile .proto files into C++ sources.
#
# `PROTOC_EXECUTABLE` and `GRPC_CPP_PLUGIN_EXECUTABLE` are expected to come
# from host-installed vcpkg tools when manifest mode is enabled.

function(proto_generate)
    cmake_parse_arguments(PG "" "TARGET;PROTO_DIR" "PROTO_FILES" ${ARGN})

    if(NOT PG_TARGET OR NOT PG_PROTO_DIR OR NOT PG_PROTO_FILES)
        message(FATAL_ERROR "proto_generate requires TARGET, PROTO_DIR and PROTO_FILES")
    endif()

    if(NOT PROTOC_EXECUTABLE)
        message(FATAL_ERROR "protoc executable not found")
    endif()

    if(NOT GRPC_CPP_PLUGIN_EXECUTABLE)
        message(FATAL_ERROR "grpc_cpp_plugin executable not found")
    endif()

    set(_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY "${_gen_dir}")

    set(_generated_sources "")

    foreach(_proto ${PG_PROTO_FILES})
        get_filename_component(_rel_dir "${_proto}" DIRECTORY)
        get_filename_component(_name_we "${_proto}" NAME_WE)

        if(_rel_dir)
            file(MAKE_DIRECTORY "${_gen_dir}/${_rel_dir}")
            set(_prefix "${_gen_dir}/${_rel_dir}/${_name_we}")
        else()
            set(_prefix "${_gen_dir}/${_name_we}")
        endif()

        set(_pb_src   "${_prefix}.pb.cc")
        set(_pb_hdr   "${_prefix}.pb.h")
        set(_grpc_src "${_prefix}.grpc.pb.cc")
        set(_grpc_hdr "${_prefix}.grpc.pb.h")

        add_custom_command(
            OUTPUT "${_pb_src}" "${_pb_hdr}" "${_grpc_src}" "${_grpc_hdr}"
            COMMAND ${PROTOC_EXECUTABLE}
            ARGS
                --proto_path=${PG_PROTO_DIR}
                --cpp_out=${_gen_dir}
                --grpc_out=${_gen_dir}
                --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
                ${PG_PROTO_DIR}/${_proto}
            DEPENDS ${PG_PROTO_DIR}/${_proto}
            COMMENT "Generating C++ from ${_proto}"
            VERBATIM
        )

        list(APPEND _generated_sources
            "${_pb_src}"
            "${_grpc_src}"
        )
    endforeach()

    add_library(${PG_TARGET} STATIC ${_generated_sources})
    target_include_directories(${PG_TARGET} PUBLIC "${_gen_dir}")
    target_link_libraries(${PG_TARGET}
        PUBLIC
            compiler_options
            protobuf::libprotobuf
            gRPC::grpc++
    )
endfunction()
