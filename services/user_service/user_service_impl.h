#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <grpcpp/grpcpp.h>

#include "infrastructure/storage/mysql/connection_pool.h"
#include "infrastructure/storage/redis/redis_client.h"
#include "user/user_service.grpc.pb.h"

namespace kd39::services::user {

class UserServiceImpl final : public kd39::api::user::UserService::Service {
public:
    struct Deps {
        std::shared_ptr<infrastructure::storage::mysql::ConnectionPool> mysql;
        std::shared_ptr<infrastructure::storage::redis::RedisClient> redis;
    };

    explicit UserServiceImpl(Deps deps);

    grpc::Status GetUser(grpc::ServerContext* context,
                         const kd39::api::user::GetUserRequest* request,
                         kd39::api::user::GetUserResponse* response) override;
    grpc::Status CreateUser(grpc::ServerContext* context,
                            const kd39::api::user::CreateUserRequest* request,
                            kd39::api::user::CreateUserResponse* response) override;

private:
    using UserProfile = kd39::api::user::UserProfile;

    static std::string BuildCacheKey(const std::string& user_id);
    static std::string SerializeProfile(const UserProfile& profile);
    static bool DeserializeProfile(const std::string& payload, UserProfile* profile);
    static infrastructure::storage::mysql::QueryRow ToMysqlRow(const UserProfile& profile);
    static UserProfile FromMysqlRow(const infrastructure::storage::mysql::QueryRow& row);

    std::optional<UserProfile> FindUser(const std::string& user_id);

    Deps deps_;
    std::mutex mu_;
    std::unordered_map<std::string, UserProfile> users_;
    std::atomic<long long> next_user_id_{1};
};

}  // namespace kd39::services::user
