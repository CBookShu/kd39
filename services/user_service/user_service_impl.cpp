#include "user_service_impl.h"

#include <ctime>
#include <nlohmann/json.hpp>

#include "common/log/logger.h"

namespace kd39::services::user {
namespace {
using kd39::api::user::UserProfile;
using kd39::infrastructure::storage::mysql::QueryRow;
}  // namespace

UserServiceImpl::UserServiceImpl(Deps deps)
    : deps_(std::move(deps)) {
    KD39_LOG_INFO("UserServiceImpl created");
}

std::string UserServiceImpl::BuildCacheKey(const std::string& user_id) {
    return "user:" + user_id;
}

std::string UserServiceImpl::SerializeProfile(const UserProfile& profile) {
    nlohmann::json json = {
        {"user_id", profile.user_id()},
        {"nickname", profile.nickname()},
        {"avatar", profile.avatar()},
        {"level", profile.level()},
        {"created_at", profile.created_at()},
    };
    return json.dump();
}

bool UserServiceImpl::DeserializeProfile(const std::string& payload, UserProfile* profile) {
    const auto json = nlohmann::json::parse(payload, nullptr, false);
    if (json.is_discarded()) {
        return false;
    }
    profile->set_user_id(json.value("user_id", ""));
    profile->set_nickname(json.value("nickname", ""));
    profile->set_avatar(json.value("avatar", ""));
    profile->set_level(json.value("level", 1));
    profile->set_created_at(json.value("created_at", 0LL));
    return true;
}

QueryRow UserServiceImpl::ToMysqlRow(const UserProfile& profile) {
    return {
        {"user_id", profile.user_id()},
        {"nickname", profile.nickname()},
        {"avatar", profile.avatar()},
        {"level", std::to_string(profile.level())},
        {"created_at", std::to_string(profile.created_at())},
    };
}

UserProfile UserServiceImpl::FromMysqlRow(const QueryRow& row) {
    UserProfile profile;
    if (auto it = row.find("user_id"); it != row.end()) profile.set_user_id(it->second);
    if (auto it = row.find("nickname"); it != row.end()) profile.set_nickname(it->second);
    if (auto it = row.find("avatar"); it != row.end()) profile.set_avatar(it->second);
    if (auto it = row.find("level"); it != row.end()) profile.set_level(std::stoi(it->second));
    if (auto it = row.find("created_at"); it != row.end()) profile.set_created_at(std::stoll(it->second));
    return profile;
}

std::optional<UserServiceImpl::UserProfile> UserServiceImpl::FindUser(const std::string& user_id) {
    if (deps_.redis) {
        if (auto cached = deps_.redis->Get(BuildCacheKey(user_id))) {
            UserProfile profile;
            if (DeserializeProfile(*cached, &profile)) {
                return profile;
            }
        }
    }

    if (deps_.mysql) {
        auto row = deps_.mysql->GetRow("users", "user_id", user_id);
        if (row.has_value()) {
            auto profile = FromMysqlRow(*row);
            if (deps_.redis) {
                deps_.redis->Set(BuildCacheKey(user_id), SerializeProfile(profile));
            }
            std::scoped_lock lock(mu_);
            users_[user_id] = profile;
            return profile;
        }
    }

    std::scoped_lock lock(mu_);
    if (auto it = users_.find(user_id); it != users_.end()) {
        return it->second;
    }
    return std::nullopt;
}

grpc::Status UserServiceImpl::GetUser(grpc::ServerContext*,
                                      const kd39::api::user::GetUserRequest* request,
                                      kd39::api::user::GetUserResponse* response) {
    auto profile = FindUser(request->user_id());
    if (!profile.has_value()) {
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "user not found");
    }
    *response->mutable_profile() = *profile;
    return grpc::Status::OK;
}

grpc::Status UserServiceImpl::CreateUser(grpc::ServerContext*,
                                         const kd39::api::user::CreateUserRequest* request,
                                         kd39::api::user::CreateUserResponse* response) {
    UserProfile profile;
    profile.set_user_id("user_" + std::to_string(next_user_id_.fetch_add(1)));
    profile.set_nickname(request->nickname().empty() ? "anonymous" : request->nickname());
    profile.set_avatar("");
    profile.set_level(1);
    profile.set_created_at(static_cast<long long>(std::time(nullptr)));

    {
        std::scoped_lock lock(mu_);
        users_[profile.user_id()] = profile;
    }
    if (deps_.mysql) {
        deps_.mysql->UpsertRow("users", ToMysqlRow(profile), {"user_id"});
    }
    if (deps_.redis) {
        deps_.redis->Set(BuildCacheKey(profile.user_id()), SerializeProfile(profile));
    }

    *response->mutable_profile() = profile;
    return grpc::Status::OK;
}

}  // namespace kd39::services::user
