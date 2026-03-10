#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <grpcpp/grpcpp.h>

#include "game/game_service.grpc.pb.h"
#include "infrastructure/storage/redis/redis_client.h"

namespace kd39::services::game {

class GameServiceImpl final : public kd39::api::game::GameService::Service {
public:
    struct Deps {
        std::shared_ptr<infrastructure::storage::redis::RedisClient> redis;
    };

    explicit GameServiceImpl(Deps deps);

    grpc::Status CreateRoom(grpc::ServerContext* context,
                            const kd39::api::game::CreateRoomRequest* request,
                            kd39::api::game::CreateRoomResponse* response) override;
    grpc::Status JoinRoom(grpc::ServerContext* context,
                          const kd39::api::game::JoinRoomRequest* request,
                          kd39::api::game::JoinRoomResponse* response) override;

private:
    struct RoomState {
        std::string room_name;
        int max_players = 0;
        std::unordered_set<std::string> members;
    };

    static std::string BuildRoomCacheKey(const std::string& room_id);

    Deps deps_;
    std::mutex mu_;
    std::unordered_map<std::string, RoomState> rooms_;
    std::atomic<long long> next_room_id_{1};
};

}  // namespace kd39::services::game
