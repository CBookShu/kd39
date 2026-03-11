#include "game_service_impl.h"

#include <nlohmann/json.hpp>

#include "common/log/logger.h"

namespace kd39::services::game {

GameServiceImpl::GameServiceImpl(Deps deps)
    : deps_(std::move(deps)) {
    KD39_LOG_INFO("GameServiceImpl created");
}

std::string GameServiceImpl::BuildRoomCacheKey(const std::string& room_id) {
    return "room:" + room_id;
}

grpc::Status GameServiceImpl::CreateRoom(grpc::ServerContext*,
                                         const kd39::api::game::CreateRoomRequest* request,
                                         kd39::api::game::CreateRoomResponse* response) {
    if (request->room_name().empty() || request->max_players() <= 0) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "room_name and max_players are required");
    }

    const auto room_id = "room_" + std::to_string(next_room_id_.fetch_add(1));
    RoomState state;
    state.room_name = request->room_name();
    state.max_players = request->max_players();

    {
        std::scoped_lock lock(mu_);
        rooms_[room_id] = state;
    }

    if (deps_.redis) {
        nlohmann::json payload = {
            {"room_id", room_id},
            {"room_name", state.room_name},
            {"max_players", state.max_players},
            {"members", nlohmann::json::array()},
        };
        deps_.redis->Set(BuildRoomCacheKey(room_id), payload.dump());
    }

    response->set_room_id(room_id);
    return grpc::Status::OK;
}

grpc::Status GameServiceImpl::JoinRoom(grpc::ServerContext*,
                                       const kd39::api::game::JoinRoomRequest* request,
                                       kd39::api::game::JoinRoomResponse* response) {
    std::scoped_lock lock(mu_);
    auto it = rooms_.find(request->room_id());
    if (it == rooms_.end()) {
        response->set_success(false);
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "room not found");
    }
    if (static_cast<int>(it->second.members.size()) >= it->second.max_players) {
        response->set_success(false);
        return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "room full");
    }
    it->second.members.insert(request->user_id());
    response->set_success(true);
    return grpc::Status::OK;
}

}  // namespace kd39::services::game
