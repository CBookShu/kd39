#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <grpcpp/grpcpp.h>

#include "config/config_service.grpc.pb.h"
#include "common/context/request_context.h"
#include "framework/governance/circuit_breaker.h"
#include "framework/governance/rate_limiter.h"
#include "game/game_service.grpc.pb.h"
#include "infrastructure/coordination/service_registry.h"
#include "user/user_service.grpc.pb.h"

namespace kd39::gateways::access {

struct RouterTargets {
    std::string config_service_target = "127.0.0.1:50051";
    std::string user_service_target = "127.0.0.1:50052";
    std::string game_service_target = "127.0.0.1:50053";
};

struct RouterRuntimeConfig {
    int grpc_timeout_ms = 800;
    int retry_attempts = 2;
    int retry_backoff_ms = 50;
};

struct RouteResult {
    int status_code = 200;
    std::string body = "{}";
    std::string error_code;
    std::string downstream_service;
    grpc::StatusCode downstream_status = grpc::StatusCode::OK;
};

class GrpcRouter {
public:
    explicit GrpcRouter(RouterTargets targets,
                        std::shared_ptr<infrastructure::coordination::ServiceResolver> resolver = nullptr,
                        RouterRuntimeConfig runtime_config = {});

    std::string Route(const std::string& path,
                      const std::string& body,
                      const common::RequestContext& ctx);
    RouteResult RouteWithStatus(const std::string& path,
                                const std::string& body,
                                const common::RequestContext& ctx);

    RouterRuntimeConfig GetRuntimeConfig() const;
    void UpdateRuntimeConfig(const RouterRuntimeConfig& config);

private:
    std::string ResolveTarget(const std::string& service_name,
                              const std::string& fallback,
                              const std::string& traffic_tag) const;
    framework::governance::CircuitBreaker& BreakerFor(const std::string& service_name);
    framework::governance::TokenBucketRateLimiter& RateLimiterFor(const std::string& service_name);

    std::shared_ptr<api::config::ConfigService::Stub> GetConfigStub(const std::string& target);
    std::shared_ptr<api::user::UserService::Stub> GetUserStub(const std::string& target);
    std::shared_ptr<api::game::GameService::Stub> GetGameStub(const std::string& target);

    RouterTargets targets_;
    std::shared_ptr<infrastructure::coordination::ServiceResolver> resolver_;

    mutable std::mutex config_mu_;
    RouterRuntimeConfig runtime_config_;

    mutable std::mutex cache_mu_;
    std::unordered_map<std::string, std::shared_ptr<grpc::Channel>> channels_;
    std::unordered_map<std::string, std::shared_ptr<api::config::ConfigService::Stub>> config_stubs_;
    std::unordered_map<std::string, std::shared_ptr<api::user::UserService::Stub>> user_stubs_;
    std::unordered_map<std::string, std::shared_ptr<api::game::GameService::Stub>> game_stubs_;

    framework::governance::CircuitBreaker config_breaker_;
    framework::governance::CircuitBreaker user_breaker_;
    framework::governance::CircuitBreaker game_breaker_;
    framework::governance::TokenBucketRateLimiter config_rate_limiter_;
    framework::governance::TokenBucketRateLimiter user_rate_limiter_;
    framework::governance::TokenBucketRateLimiter game_rate_limiter_;
};

}  // namespace kd39::gateways::access
