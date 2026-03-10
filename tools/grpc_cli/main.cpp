#include <iostream>
#include <string>
#include <unordered_map>

#include <grpcpp/grpcpp.h>
#include <nlohmann/json.hpp>

#include "config/config_service.grpc.pb.h"
#include "game/game_service.grpc.pb.h"
#include "user/user_service.grpc.pb.h"

namespace {
using Args = std::unordered_map<std::string, std::string>;

Args ParseArgs(int argc, char* argv[], int start) {
    Args args;
    for (int i = start; i < argc; ++i) {
        std::string token = argv[i];
        if (!token.starts_with("--")) {
            continue;
        }
        token.erase(0, 2);
        const auto pos = token.find('=');
        if (pos == std::string::npos) {
            args[token] = "true";
        } else {
            args[token.substr(0, pos)] = token.substr(pos + 1);
        }
    }
    return args;
}

std::string GetOr(const Args& args, const std::string& key, const std::string& fallback = "") {
    if (auto it = args.find(key); it != args.end()) {
        return it->second;
    }
    return fallback;
}

void AddMetadata(grpc::ClientContext& ctx, const Args& args) {
    if (auto v = GetOr(args, "request-id"); !v.empty()) ctx.AddMetadata("x-request-id", v);
    if (auto v = GetOr(args, "trace-id"); !v.empty()) ctx.AddMetadata("x-trace-id", v);
    if (auto v = GetOr(args, "user-id"); !v.empty()) ctx.AddMetadata("x-user-id", v);
    if (auto v = GetOr(args, "tag"); !v.empty()) ctx.AddMetadata("x-traffic-tag", v);
    if (auto v = GetOr(args, "client-version"); !v.empty()) ctx.AddMetadata("x-client-version", v);
    if (auto v = GetOr(args, "zone"); !v.empty()) ctx.AddMetadata("x-zone", v);
}

std::string DefaultTarget(const std::string& service) {
    if (service == "config") return "127.0.0.1:50051";
    if (service == "user") return "127.0.0.1:50052";
    if (service == "game") return "127.0.0.1:50053";
    return "127.0.0.1:50051";
}

void PrintUsage() {
    std::cout << "Usage:\n"
              << "  grpc_cli config get --namespace=ns --key=k [--target=host:port]\n"
              << "  grpc_cli config batch-get --namespace=ns\n"
              << "  grpc_cli config publish --namespace=ns --key=k --value=v [--environment=dev]\n"
              << "  grpc_cli config watch --namespace=ns [--since-version=0]\n"
              << "  grpc_cli user get --user-id=id\n"
              << "  grpc_cli user create --nickname=name\n"
              << "  grpc_cli game create-room --room-name=name --max-players=4\n"
              << "  grpc_cli game join-room --room-id=id --user-id=user_1\n";
}

int RunConfig(const std::string& method, const Args& args) {
    auto channel = grpc::CreateChannel(GetOr(args, "target", DefaultTarget("config")), grpc::InsecureChannelCredentials());
    auto stub = kd39::api::config::ConfigService::NewStub(channel);
    grpc::ClientContext ctx;
    AddMetadata(ctx, args);

    if (method == "get") {
        kd39::api::config::GetConfigRequest req;
        req.set_namespace_name(GetOr(args, "namespace"));
        req.set_key(GetOr(args, "key"));
        kd39::api::config::GetConfigResponse resp;
        auto status = stub->GetConfig(&ctx, req, &resp);
        if (!status.ok()) {
            std::cerr << status.error_message() << "\n";
            return 1;
        }
        nlohmann::json json = {
            {"namespace_name", resp.entry().namespace_name()},
            {"key", resp.entry().key()},
            {"value", resp.entry().value()},
            {"version", resp.entry().version()},
            {"environment", resp.entry().environment()},
        };
        std::cout << json.dump(2) << "\n";
        return 0;
    }
    if (method == "batch-get") {
        kd39::api::config::BatchGetConfigRequest req;
        req.set_namespace_name(GetOr(args, "namespace"));
        kd39::api::config::BatchGetConfigResponse resp;
        auto status = stub->BatchGetConfig(&ctx, req, &resp);
        if (!status.ok()) {
            std::cerr << status.error_message() << "\n";
            return 1;
        }
        nlohmann::json items = nlohmann::json::array();
        for (const auto& entry : resp.entries()) {
            items.push_back({
                {"namespace_name", entry.namespace_name()},
                {"key", entry.key()},
                {"value", entry.value()},
                {"version", entry.version()},
                {"environment", entry.environment()},
            });
        }
        std::cout << items.dump(2) << "\n";
        return 0;
    }
    if (method == "publish") {
        kd39::api::config::PublishConfigRequest req;
        auto* entry = req.mutable_entry();
        entry->set_namespace_name(GetOr(args, "namespace"));
        entry->set_key(GetOr(args, "key"));
        entry->set_value(GetOr(args, "value"));
        entry->set_environment(GetOr(args, "environment", "dev"));
        kd39::api::config::PublishConfigResponse resp;
        auto status = stub->PublishConfig(&ctx, req, &resp);
        if (!status.ok()) {
            std::cerr << status.error_message() << "\n";
            return 1;
        }
        std::cout << "published version=" << resp.version() << "\n";
        return 0;
    }
    if (method == "watch") {
        kd39::api::config::WatchConfigRequest req;
        req.set_namespace_name(GetOr(args, "namespace"));
        req.set_since_version(std::stoll(GetOr(args, "since-version", "0")));
        auto reader = stub->WatchConfig(&ctx, req);
        kd39::api::config::WatchConfigResponse resp;
        while (reader->Read(&resp)) {
            for (const auto& entry : resp.updates()) {
                std::cout << entry.namespace_name() << "/" << entry.key() << " = " << entry.value() << " version=" << entry.version() << "\n";
            }
        }
        auto status = reader->Finish();
        return status.ok() ? 0 : 1;
    }
    return 1;
}

int RunUser(const std::string& method, const Args& args) {
    auto channel = grpc::CreateChannel(GetOr(args, "target", DefaultTarget("user")), grpc::InsecureChannelCredentials());
    auto stub = kd39::api::user::UserService::NewStub(channel);
    grpc::ClientContext ctx;
    AddMetadata(ctx, args);

    if (method == "get") {
        kd39::api::user::GetUserRequest req;
        req.set_user_id(GetOr(args, "user-id"));
        kd39::api::user::GetUserResponse resp;
        auto status = stub->GetUser(&ctx, req, &resp);
        if (!status.ok()) {
            std::cerr << status.error_message() << "\n";
            return 1;
        }
        nlohmann::json json = {
            {"user_id", resp.profile().user_id()},
            {"nickname", resp.profile().nickname()},
            {"avatar", resp.profile().avatar()},
            {"level", resp.profile().level()},
            {"created_at", resp.profile().created_at()},
        };
        std::cout << json.dump(2) << "\n";
        return 0;
    }
    if (method == "create") {
        kd39::api::user::CreateUserRequest req;
        req.set_nickname(GetOr(args, "nickname"));
        kd39::api::user::CreateUserResponse resp;
        auto status = stub->CreateUser(&ctx, req, &resp);
        if (!status.ok()) {
            std::cerr << status.error_message() << "\n";
            return 1;
        }
        std::cout << resp.profile().user_id() << "\n";
        return 0;
    }
    return 1;
}
int RunGame(const std::string& method, const Args& args) {
    auto channel = grpc::CreateChannel(GetOr(args, "target", DefaultTarget("game")), grpc::InsecureChannelCredentials());
    auto stub = kd39::api::game::GameService::NewStub(channel);
    grpc::ClientContext ctx;
    AddMetadata(ctx, args);

    if (method == "create-room") {
        kd39::api::game::CreateRoomRequest req;
        req.set_room_name(GetOr(args, "room-name"));
        req.set_max_players(std::stoi(GetOr(args, "max-players", "4")));
        kd39::api::game::CreateRoomResponse resp;
        auto status = stub->CreateRoom(&ctx, req, &resp);
        if (!status.ok()) {
            std::cerr << status.error_message() << "\n";
            return 1;
        }
        std::cout << resp.room_id() << "\n";
        return 0;
    }
    if (method == "join-room") {
        kd39::api::game::JoinRoomRequest req;
        req.set_room_id(GetOr(args, "room-id"));
        req.set_user_id(GetOr(args, "user-id"));
        kd39::api::game::JoinRoomResponse resp;
        auto status = stub->JoinRoom(&ctx, req, &resp);
        if (!status.ok()) {
            std::cerr << status.error_message() << "\n";
            return 1;
        }
        std::cout << (resp.success() ? "joined" : "failed") << "\n";
        return resp.success() ? 0 : 1;
    }
    return 1;
}
}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        PrintUsage();
        return 1;
    }
    const std::string service = argv[1];
    const std::string method = argv[2];
    const auto args = ParseArgs(argc, argv, 3);

    if (service == "config") return RunConfig(method, args);
    if (service == "user") return RunUser(method, args);
    if (service == "game") return RunGame(method, args);

    PrintUsage();
    return 1;
}
