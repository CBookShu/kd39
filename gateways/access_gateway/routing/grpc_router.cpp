#include "access_gateway/routing/grpc_router.h"

#include <grpcpp/grpcpp.h>
#include <nlohmann/json.hpp>

#include "config/config_service.grpc.pb.h"
#include "framework/governance/traffic_router.h"
#include "game/game_service.grpc.pb.h"
#include "user/user_service.grpc.pb.h"
#include "infrastructure/observability/metrics.h"
#include "infrastructure/observability/telemetry.h"

namespace kd39::gateways::access {
namespace {
void AddMetadata(grpc::ClientContext& grpc_ctx, const common::RequestContext& ctx) {
    if (!ctx.request_id.empty()) grpc_ctx.AddMetadata(common::RequestContext::kMetaRequestId, ctx.request_id);
    if (!ctx.trace_id.empty()) grpc_ctx.AddMetadata(common::RequestContext::kMetaTraceId, ctx.trace_id);
    if (!ctx.user_id.empty()) grpc_ctx.AddMetadata(common::RequestContext::kMetaUserId, ctx.user_id);
    if (!ctx.traffic_tag.empty()) grpc_ctx.AddMetadata(common::RequestContext::kMetaTrafficTag, ctx.traffic_tag);
    if (!ctx.client_version.empty()) grpc_ctx.AddMetadata(common::RequestContext::kMetaClientVersion, ctx.client_version);
    if (!ctx.zone.empty()) grpc_ctx.AddMetadata(common::RequestContext::kMetaZone, ctx.zone);
}

nlohmann::json ErrorJson(const std::string& message) {
    return {{"error", message}};
}
}  // namespace

GrpcRouter::GrpcRouter(RouterTargets targets,
                       std::shared_ptr<infrastructure::coordination::ServiceResolver> resolver)
    : targets_(std::move(targets)), resolver_(std::move(resolver)) {}

std::string GrpcRouter::ResolveTarget(const std::string& service_name,
                                      const std::string& fallback,
                                      const std::string& traffic_tag) const {
    if (!resolver_) {
        return fallback;
    }
    const auto instances = resolver_->Resolve(service_name);
    if (instances.empty()) {
        return fallback;
    }
    kd39::framework::governance::TrafficRouter router;
    const auto selected = router.Select(instances, traffic_tag);
    if (selected.host.empty() || selected.port == 0) {
        return fallback;
    }
    return selected.host + ":" + std::to_string(selected.port);
}

std::string GrpcRouter::Route(const std::string& path,
                              const std::string& body,
                              const common::RequestContext& ctx) {
    auto span = kd39::infrastructure::observability::Tracer::Instance().StartSpan("gateway.grpc_route");
    kd39::infrastructure::observability::MetricsRegistry::Instance().Increment("gateway_requests_total");
    const auto json = nlohmann::json::parse(body.empty() ? "{}" : body, nullptr, false);
    if (json.is_discarded()) {
        return ErrorJson("invalid_json").dump();
    }

    if (path == "/config/get") {
        auto channel = grpc::CreateChannel(ResolveTarget("config_service", targets_.config_service_target, ctx.traffic_tag), grpc::InsecureChannelCredentials());
        auto stub = kd39::api::config::ConfigService::NewStub(channel);
        grpc::ClientContext grpc_ctx;
        AddMetadata(grpc_ctx, ctx);
        kd39::api::config::GetConfigRequest req;
        req.set_namespace_name(json.value("namespace_name", ""));
        req.set_key(json.value("key", ""));
        kd39::api::config::GetConfigResponse resp;
        auto status = stub->GetConfig(&grpc_ctx, req, &resp);
        if (!status.ok()) return ErrorJson(status.error_message()).dump();
        return nlohmann::json{{"namespace_name", resp.entry().namespace_name()}, {"key", resp.entry().key()}, {"value", resp.entry().value()}, {"version", resp.entry().version()}, {"environment", resp.entry().environment()}}.dump();
    }

    if (path == "/config/publish") {
        auto channel = grpc::CreateChannel(ResolveTarget("config_service", targets_.config_service_target, ctx.traffic_tag), grpc::InsecureChannelCredentials());
        auto stub = kd39::api::config::ConfigService::NewStub(channel);
        grpc::ClientContext grpc_ctx;
        AddMetadata(grpc_ctx, ctx);
        kd39::api::config::PublishConfigRequest req;
        auto* entry = req.mutable_entry();
        entry->set_namespace_name(json.value("namespace_name", ""));
        entry->set_key(json.value("key", ""));
        entry->set_value(json.value("value", ""));
        entry->set_environment(json.value("environment", "dev"));
        kd39::api::config::PublishConfigResponse resp;
        auto status = stub->PublishConfig(&grpc_ctx, req, &resp);
        if (!status.ok()) return ErrorJson(status.error_message()).dump();
        return nlohmann::json{{"version", resp.version()}}.dump();
    }

    if (path == "/user/get") {
        auto channel = grpc::CreateChannel(ResolveTarget("user_service", targets_.user_service_target, ctx.traffic_tag), grpc::InsecureChannelCredentials());
        auto stub = kd39::api::user::UserService::NewStub(channel);
        grpc::ClientContext grpc_ctx;
        AddMetadata(grpc_ctx, ctx);
        kd39::api::user::GetUserRequest req;
        req.set_user_id(json.value("user_id", ctx.user_id));
        kd39::api::user::GetUserResponse resp;
        auto status = stub->GetUser(&grpc_ctx, req, &resp);
        if (!status.ok()) return ErrorJson(status.error_message()).dump();
        return nlohmann::json{{"user_id", resp.profile().user_id()}, {"nickname", resp.profile().nickname()}, {"avatar", resp.profile().avatar()}, {"level", resp.profile().level()}, {"created_at", resp.profile().created_at()}}.dump();
    }

    if (path == "/user/create") {
        auto channel = grpc::CreateChannel(ResolveTarget("user_service", targets_.user_service_target, ctx.traffic_tag), grpc::InsecureChannelCredentials());
        auto stub = kd39::api::user::UserService::NewStub(channel);
        grpc::ClientContext grpc_ctx;
        AddMetadata(grpc_ctx, ctx);
        kd39::api::user::CreateUserRequest req;
        req.set_nickname(json.value("nickname", ""));
        kd39::api::user::CreateUserResponse resp;
        auto status = stub->CreateUser(&grpc_ctx, req, &resp);
        if (!status.ok()) return ErrorJson(status.error_message()).dump();
        return nlohmann::json{{"user_id", resp.profile().user_id()}, {"nickname", resp.profile().nickname()}}.dump();
    }

    if (path == "/game/create-room") {
        auto channel = grpc::CreateChannel(ResolveTarget("game_service", targets_.game_service_target, ctx.traffic_tag), grpc::InsecureChannelCredentials());
        auto stub = kd39::api::game::GameService::NewStub(channel);
        grpc::ClientContext grpc_ctx;
        AddMetadata(grpc_ctx, ctx);
        kd39::api::game::CreateRoomRequest req;
        req.set_room_name(json.value("room_name", ""));
        req.set_max_players(json.value("max_players", 4));
        kd39::api::game::CreateRoomResponse resp;
        auto status = stub->CreateRoom(&grpc_ctx, req, &resp);
        if (!status.ok()) return ErrorJson(status.error_message()).dump();
        return nlohmann::json{{"room_id", resp.room_id()}}.dump();
    }

    if (path == "/game/join-room") {
        auto channel = grpc::CreateChannel(ResolveTarget("game_service", targets_.game_service_target, ctx.traffic_tag), grpc::InsecureChannelCredentials());
        auto stub = kd39::api::game::GameService::NewStub(channel);
        grpc::ClientContext grpc_ctx;
        AddMetadata(grpc_ctx, ctx);
        kd39::api::game::JoinRoomRequest req;
        req.set_room_id(json.value("room_id", ""));
        req.set_user_id(json.value("user_id", ctx.user_id));
        kd39::api::game::JoinRoomResponse resp;
        auto status = stub->JoinRoom(&grpc_ctx, req, &resp);
        if (!status.ok()) return ErrorJson(status.error_message()).dump();
        return nlohmann::json{{"success", resp.success()}}.dump();
    }

    return ErrorJson("route_not_found").dump();
}

}  // namespace kd39::gateways::access
