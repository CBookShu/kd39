include(ProtoGen)

proto_generate(
    TARGET   api_proto
    PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto"
    PROTO_FILES
        common/context.proto
        config/config_service.proto
        user/user_service.proto
        game/game_service.proto
)
