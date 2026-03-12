#include "access_gateway/routing/grpc_router.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <future>
#include <thread>

#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/cobalt/op.hpp>
#include <boost/cobalt/spawn.hpp>
#include <grpcpp/grpcpp.h>
#include <nlohmann/json.hpp>

#include "common/log/logger.h"
#include "framework/governance/traffic_router.h"
#include "infrastructure/observability/metrics.h"
#include "infrastructure/observability/telemetry.h"

namespace kd39::gateways::access {
namespace {
namespace net = boost::asio;
namespace cobalt = boost::cobalt;

using ConfigGetRpc = agrpc::ClientRPC<&api::config::ConfigService::Stub::PrepareAsyncGetConfig>;
using ConfigPublishRpc = agrpc::ClientRPC<&api::config::ConfigService::Stub::PrepareAsyncPublishConfig>;
using UserGetRpc = agrpc::ClientRPC<&api::user::UserService::Stub::PrepareAsyncGetUser>;
using UserCreateRpc = agrpc::ClientRPC<&api::user::UserService::Stub::PrepareAsyncCreateUser>;
using GameCreateRoomRpc = agrpc::ClientRPC<&api::game::GameService::Stub::PrepareAsyncCreateRoom>;
using GameJoinRoomRpc = agrpc::ClientRPC<&api::game::GameService::Stub::PrepareAsyncJoinRoom>;

void AddMetadata(grpc::ClientContext& grpc_ctx, const common::RequestContext& ctx) {
    if (!ctx.request_id.empty()) grpc_ctx.AddMetadata(common::RequestContext::kMetaRequestId, ctx.request_id);
    if (!ctx.trace_id.empty()) grpc_ctx.AddMetadata(common::RequestContext::kMetaTraceId, ctx.trace_id);
    if (!ctx.user_id.empty()) grpc_ctx.AddMetadata(common::RequestContext::kMetaUserId, ctx.user_id);
    if (!ctx.traffic_tag.empty()) grpc_ctx.AddMetadata(common::RequestContext::kMetaTrafficTag, ctx.traffic_tag);
    if (!ctx.client_version.empty()) grpc_ctx.AddMetadata(common::RequestContext::kMetaClientVersion, ctx.client_version);
    if (!ctx.zone.empty()) grpc_ctx.AddMetadata(common::RequestContext::kMetaZone, ctx.zone);
}

nlohmann::json ErrorJson(const std::string& code, const std::string& message, int grpc_status = 0) {
    return {{"error", message}, {"code", code}, {"grpc_status", grpc_status}};
}

std::string ToMetricSafe(std::string value) {
    for (auto& ch : value) {
        if (!std::isalnum(static_cast<unsigned char>(ch))) {
            ch = '_';
        }
    }
    return value;
}

int HttpStatusFromGrpc(grpc::StatusCode code) {
    switch (code) {
        case grpc::StatusCode::INVALID_ARGUMENT:
            return 400;
        case grpc::StatusCode::UNAUTHENTICATED:
            return 401;
        case grpc::StatusCode::PERMISSION_DENIED:
            return 403;
        case grpc::StatusCode::NOT_FOUND:
            return 404;
        case grpc::StatusCode::ALREADY_EXISTS:
            return 409;
        case grpc::StatusCode::FAILED_PRECONDITION:
            return 412;
        case grpc::StatusCode::DEADLINE_EXCEEDED:
            return 504;
        case grpc::StatusCode::UNAVAILABLE:
            return 503;
        case grpc::StatusCode::RESOURCE_EXHAUSTED:
            return 429;
        default:
            return 502;
    }
}

bool IsRetryable(grpc::StatusCode code) {
    return code == grpc::StatusCode::UNAVAILABLE || code == grpc::StatusCode::DEADLINE_EXCEEDED;
}

std::size_t DefaultGrpcThreadCount() {
    const auto hc = std::thread::hardware_concurrency();
    if (hc == 0) {
        return 2;
    }
    return std::clamp<std::size_t>(hc / 2, 1, 4);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
template <typename RpcType, typename StubT, typename RequestT, typename ResponseT>
cobalt::task<grpc::Status> InvokeWithResilienceAsync(agrpc::GrpcContext& grpc_context,
                                                     const common::RequestContext& ctx,
                                                     const RouterRuntimeConfig& runtime,
                                                     framework::governance::CircuitBreaker& breaker,
                                                     framework::governance::TokenBucketRateLimiter& limiter,
                                                     StubT& stub,
                                                     const RequestT& req,
                                                     ResponseT& resp) {
    if (!limiter.Allow()) {
        co_return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED, "gateway rate limit exceeded");
    }
    if (!breaker.AllowRequest()) {
        co_return grpc::Status(grpc::StatusCode::UNAVAILABLE, "circuit breaker open");
    }

    const int attempts = std::max(1, runtime.retry_attempts);
    const auto timeout = std::chrono::milliseconds(std::max(100, runtime.grpc_timeout_ms));
    const auto backoff = std::chrono::milliseconds(std::max(0, runtime.retry_backoff_ms));
    grpc::Status last_status;

    for (int attempt = 1; attempt <= attempts; ++attempt) {
        grpc::ClientContext grpc_ctx;
        grpc_ctx.set_deadline(std::chrono::system_clock::now() + timeout);
        AddMetadata(grpc_ctx, ctx);
        last_status = co_await RpcType::request(grpc_context, stub, grpc_ctx, req, resp, cobalt::use_op);
        if (last_status.ok()) {
            breaker.RecordSuccess();
            co_return last_status;
        }

        breaker.RecordFailure();
        if (!IsRetryable(last_status.error_code()) || attempt == attempts) {
            co_return last_status;
        }
        if (backoff.count() > 0) {
            net::steady_timer timer(grpc_context);
            timer.expires_after(backoff);
            boost::system::error_code ec;
            co_await timer.async_wait(net::redirect_error(cobalt::use_op, ec));
            if (ec == net::error::operation_aborted) {
                co_return grpc::Status(grpc::StatusCode::CANCELLED, "retry_backoff_cancelled");
            }
        }
    }

    co_return last_status;
}
}  // namespace

GrpcRouter::GrpcRouter(RouterTargets targets,
                       std::shared_ptr<infrastructure::coordination::ServiceResolver> resolver,
                       RouterRuntimeConfig runtime_config)
    : targets_(std::move(targets)),
      resolver_(std::move(resolver)),
      runtime_config_(runtime_config),
      config_breaker_(5, std::chrono::seconds(5)),
      user_breaker_(5, std::chrono::seconds(5)),
      game_breaker_(5, std::chrono::seconds(5)),
      config_rate_limiter_(500.0, 1000.0),
      user_rate_limiter_(500.0, 1000.0),
      game_rate_limiter_(500.0, 1000.0),
      grpc_thread_count_(DefaultGrpcThreadCount()),
      grpc_context_(grpc_thread_count_) {
    StartGrpcWorkers();
}

GrpcRouter::~GrpcRouter() { StopGrpcWorkers(); }

void GrpcRouter::StartGrpcWorkers() {
    if (grpc_running_.exchange(true)) {
        return;
    }
    grpc_threads_.reserve(grpc_thread_count_);
    for (std::size_t i = 0; i < grpc_thread_count_; ++i) {
        grpc_threads_.emplace_back([this] {
            while (grpc_running_.load(std::memory_order_acquire)) {
                const bool did_work = grpc_context_.run();
                if (!grpc_running_.load(std::memory_order_acquire)) {
                    break;
                }
                grpc_context_.reset();
                if (!did_work) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        });
    }
    KD39_LOG_INFO("GrpcRouter async grpc workers started: {}", grpc_thread_count_);
}

void GrpcRouter::StopGrpcWorkers() {
    if (!grpc_running_.exchange(false)) {
        return;
    }
    grpc_context_.stop();
    for (auto& thread : grpc_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    grpc_threads_.clear();
    grpc_context_.reset();
    KD39_LOG_INFO("GrpcRouter async grpc workers stopped");
}

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
    return RouteWithStatus(path, body, ctx).body;
}

RouteResult GrpcRouter::RouteWithStatus(const std::string& path,
                                        const std::string& body,
                                        const common::RequestContext& ctx) {
    try {
        auto fut = cobalt::spawn(grpc_context_, RouteWithStatusAsync(path, body, ctx), net::use_future);
        return fut.get();
    } catch (const std::exception& ex) {
        KD39_LOG_ERROR("GrpcRouter RouteWithStatus sync wrapper failed: {}", ex.what());
        return {503, ErrorJson("router_async_failed", ex.what()).dump(), "router_async_failed", "", grpc::StatusCode::UNAVAILABLE};
    }
}

boost::cobalt::task<std::string> GrpcRouter::RouteAsync(const std::string& path,
                                                        const std::string& body,
                                                        const common::RequestContext& ctx) {
    co_return (co_await RouteWithStatusAsync(path, body, ctx)).body;
}

boost::cobalt::task<RouteResult> GrpcRouter::RouteWithStatusAsync(const std::string& path,
                                                                  const std::string& body,
                                                                  const common::RequestContext& ctx) {
    auto span = kd39::infrastructure::observability::Tracer::Instance().StartSpan("gateway.grpc_route");
    const auto started_at = std::chrono::steady_clock::now();
    auto finish = [&](RouteResult result) {
        const auto latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - started_at)
                                    .count();
        kd39::infrastructure::observability::MetricsRegistry::Instance().Increment("gateway_requests_total");
        kd39::infrastructure::observability::MetricsRegistry::Instance().Increment(
            "gateway_requests_route_" + ToMetricSafe(path) + "_total");
        kd39::infrastructure::observability::MetricsRegistry::Instance().Increment(
            "gateway_requests_status_" + std::to_string(result.status_code) + "_total");
        if (!result.downstream_service.empty()) {
            kd39::infrastructure::observability::MetricsRegistry::Instance().Increment(
                "gateway_downstream_" + ToMetricSafe(result.downstream_service) + "_total");
        }
        if (result.status_code >= 500) {
            KD39_LOG_WARN("gateway route path={} status={} code={} service={} latency_ms={}",
                          path, result.status_code, result.error_code, result.downstream_service, latency_ms);
        } else {
            KD39_LOG_INFO("gateway route path={} status={} service={} latency_ms={}",
                          path, result.status_code, result.downstream_service, latency_ms);
        }
        return result;
    };

    const auto json = nlohmann::json::parse(body.empty() ? "{}" : body, nullptr, false);
    if (json.is_discarded()) {
        co_return finish({400, ErrorJson("invalid_json", "invalid_json").dump(), "invalid_json", "", grpc::StatusCode::INVALID_ARGUMENT});
    }

    const auto runtime = GetRuntimeConfig();

    if (path == "/config/get") {
        const auto target = ResolveTarget("config_service", targets_.config_service_target, ctx.traffic_tag);
        auto stub = GetConfigStub(target);
        kd39::api::config::GetConfigRequest req;
        req.set_namespace_name(json.value("namespace_name", ""));
        req.set_key(json.value("key", ""));
        kd39::api::config::GetConfigResponse resp;
        const auto status = co_await InvokeWithResilienceAsync<ConfigGetRpc>(
            grpc_context_, ctx, runtime, BreakerFor("config_service"), RateLimiterFor("config_service"), *stub, req, resp);
        if (!status.ok()) {
            co_return finish({
                HttpStatusFromGrpc(status.error_code()),
                ErrorJson("downstream_error", status.error_message(), static_cast<int>(status.error_code())).dump(),
                "downstream_error",
                "config_service",
                status.error_code()});
        }
        co_return finish({200,
                          nlohmann::json{
                              {"namespace_name", resp.entry().namespace_name()},
                              {"key", resp.entry().key()},
                              {"value", resp.entry().value()},
                              {"version", resp.entry().version()},
                              {"environment", resp.entry().environment()}}
                              .dump(),
                          "",
                          "config_service",
                          grpc::StatusCode::OK});
    }

    if (path == "/config/publish") {
        const auto target = ResolveTarget("config_service", targets_.config_service_target, ctx.traffic_tag);
        auto stub = GetConfigStub(target);
        kd39::api::config::PublishConfigRequest req;
        auto* entry = req.mutable_entry();
        entry->set_namespace_name(json.value("namespace_name", ""));
        entry->set_key(json.value("key", ""));
        entry->set_value(json.value("value", ""));
        entry->set_environment(json.value("environment", "dev"));
        kd39::api::config::PublishConfigResponse resp;
        const auto status = co_await InvokeWithResilienceAsync<ConfigPublishRpc>(
            grpc_context_, ctx, runtime, BreakerFor("config_service"), RateLimiterFor("config_service"), *stub, req, resp);
        if (!status.ok()) {
            co_return finish({
                HttpStatusFromGrpc(status.error_code()),
                ErrorJson("downstream_error", status.error_message(), static_cast<int>(status.error_code())).dump(),
                "downstream_error",
                "config_service",
                status.error_code()});
        }
        co_return finish({200, nlohmann::json{{"version", resp.version()}}.dump(), "", "config_service", grpc::StatusCode::OK});
    }

    if (path == "/user/get") {
        const auto target = ResolveTarget("user_service", targets_.user_service_target, ctx.traffic_tag);
        auto stub = GetUserStub(target);
        kd39::api::user::GetUserRequest req;
        req.set_user_id(json.value("user_id", ctx.user_id));
        kd39::api::user::GetUserResponse resp;
        const auto status = co_await InvokeWithResilienceAsync<UserGetRpc>(
            grpc_context_, ctx, runtime, BreakerFor("user_service"), RateLimiterFor("user_service"), *stub, req, resp);
        if (!status.ok()) {
            co_return finish({
                HttpStatusFromGrpc(status.error_code()),
                ErrorJson("downstream_error", status.error_message(), static_cast<int>(status.error_code())).dump(),
                "downstream_error",
                "user_service",
                status.error_code()});
        }
        co_return finish({200,
                          nlohmann::json{
                              {"user_id", resp.profile().user_id()},
                              {"nickname", resp.profile().nickname()},
                              {"avatar", resp.profile().avatar()},
                              {"level", resp.profile().level()},
                              {"created_at", resp.profile().created_at()}}
                              .dump(),
                          "",
                          "user_service",
                          grpc::StatusCode::OK});
    }

    if (path == "/user/create") {
        const auto target = ResolveTarget("user_service", targets_.user_service_target, ctx.traffic_tag);
        auto stub = GetUserStub(target);
        kd39::api::user::CreateUserRequest req;
        req.set_nickname(json.value("nickname", ""));
        kd39::api::user::CreateUserResponse resp;
        const auto status = co_await InvokeWithResilienceAsync<UserCreateRpc>(
            grpc_context_, ctx, runtime, BreakerFor("user_service"), RateLimiterFor("user_service"), *stub, req, resp);
        if (!status.ok()) {
            co_return finish({
                HttpStatusFromGrpc(status.error_code()),
                ErrorJson("downstream_error", status.error_message(), static_cast<int>(status.error_code())).dump(),
                "downstream_error",
                "user_service",
                status.error_code()});
        }
        co_return finish({200,
                          nlohmann::json{{"user_id", resp.profile().user_id()}, {"nickname", resp.profile().nickname()}}.dump(),
                          "",
                          "user_service",
                          grpc::StatusCode::OK});
    }

    if (path == "/game/create-room") {
        const auto target = ResolveTarget("game_service", targets_.game_service_target, ctx.traffic_tag);
        auto stub = GetGameStub(target);
        kd39::api::game::CreateRoomRequest req;
        req.set_room_name(json.value("room_name", ""));
        req.set_max_players(json.value("max_players", 4));
        kd39::api::game::CreateRoomResponse resp;
        const auto status = co_await InvokeWithResilienceAsync<GameCreateRoomRpc>(
            grpc_context_, ctx, runtime, BreakerFor("game_service"), RateLimiterFor("game_service"), *stub, req, resp);
        if (!status.ok()) {
            co_return finish({
                HttpStatusFromGrpc(status.error_code()),
                ErrorJson("downstream_error", status.error_message(), static_cast<int>(status.error_code())).dump(),
                "downstream_error",
                "game_service",
                status.error_code()});
        }
        co_return finish({200, nlohmann::json{{"room_id", resp.room_id()}}.dump(), "", "game_service", grpc::StatusCode::OK});
    }

    if (path == "/game/join-room") {
        const auto target = ResolveTarget("game_service", targets_.game_service_target, ctx.traffic_tag);
        auto stub = GetGameStub(target);
        kd39::api::game::JoinRoomRequest req;
        req.set_room_id(json.value("room_id", ""));
        req.set_user_id(json.value("user_id", ctx.user_id));
        kd39::api::game::JoinRoomResponse resp;
        const auto status = co_await InvokeWithResilienceAsync<GameJoinRoomRpc>(
            grpc_context_, ctx, runtime, BreakerFor("game_service"), RateLimiterFor("game_service"), *stub, req, resp);
        if (!status.ok()) {
            co_return finish({
                HttpStatusFromGrpc(status.error_code()),
                ErrorJson("downstream_error", status.error_message(), static_cast<int>(status.error_code())).dump(),
                "downstream_error",
                "game_service",
                status.error_code()});
        }
        co_return finish({200, nlohmann::json{{"success", resp.success()}}.dump(), "", "game_service", grpc::StatusCode::OK});
    }

    co_return finish({404, ErrorJson("route_not_found", "route_not_found").dump(), "route_not_found", "", grpc::StatusCode::NOT_FOUND});
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

RouterRuntimeConfig GrpcRouter::GetRuntimeConfig() const {
    std::scoped_lock lock(config_mu_);
    return runtime_config_;
}

void GrpcRouter::UpdateRuntimeConfig(const RouterRuntimeConfig& config) {
    RouterRuntimeConfig sanitized = config;
    sanitized.grpc_timeout_ms = std::max(100, sanitized.grpc_timeout_ms);
    sanitized.retry_attempts = std::max(1, sanitized.retry_attempts);
    sanitized.retry_backoff_ms = std::max(0, sanitized.retry_backoff_ms);
    std::scoped_lock lock(config_mu_);
    runtime_config_ = sanitized;
}

framework::governance::CircuitBreaker& GrpcRouter::BreakerFor(const std::string& service_name) {
    if (service_name == "config_service") {
        return config_breaker_;
    }
    if (service_name == "user_service") {
        return user_breaker_;
    }
    return game_breaker_;
}

framework::governance::TokenBucketRateLimiter& GrpcRouter::RateLimiterFor(const std::string& service_name) {
    if (service_name == "config_service") {
        return config_rate_limiter_;
    }
    if (service_name == "user_service") {
        return user_rate_limiter_;
    }
    return game_rate_limiter_;
}

std::shared_ptr<api::config::ConfigService::Stub> GrpcRouter::GetConfigStub(const std::string& target) {
    std::scoped_lock lock(cache_mu_);
    if (auto it = config_stubs_.find(target); it != config_stubs_.end()) {
        return it->second;
    }
    auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    auto stub = std::shared_ptr<api::config::ConfigService::Stub>(api::config::ConfigService::NewStub(channel).release());
    channels_[target] = channel;
    config_stubs_[target] = stub;
    return stub;
}

std::shared_ptr<api::user::UserService::Stub> GrpcRouter::GetUserStub(const std::string& target) {
    std::scoped_lock lock(cache_mu_);
    if (auto it = user_stubs_.find(target); it != user_stubs_.end()) {
        return it->second;
    }
    auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    auto stub = std::shared_ptr<api::user::UserService::Stub>(api::user::UserService::NewStub(channel).release());
    channels_[target] = channel;
    user_stubs_[target] = stub;
    return stub;
}

std::shared_ptr<api::game::GameService::Stub> GrpcRouter::GetGameStub(const std::string& target) {
    std::scoped_lock lock(cache_mu_);
    if (auto it = game_stubs_.find(target); it != game_stubs_.end()) {
        return it->second;
    }
    auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    auto stub = std::shared_ptr<api::game::GameService::Stub>(api::game::GameService::NewStub(channel).release());
    channels_[target] = channel;
    game_stubs_[target] = stub;
    return stub;
}

}  // namespace kd39::gateways::access
