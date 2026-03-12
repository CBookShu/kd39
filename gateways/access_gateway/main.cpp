#include <memory>

#include "access_gateway/auth/auth_middleware.h"
#include "access_gateway/http/http_server.h"
#include "access_gateway/routing/grpc_router.h"
#include "access_gateway/ws/ws_server.h"
#include "common/config/app_config.h"
#include "common/log/logger.h"
#include "framework/app/application.h"
#include "infrastructure/coordination/impl/etcd/factory.h"

int main(int argc, char* argv[]) {
    constexpr const char* kServiceName = "access_gateway";
    const std::string config_path = argc > 1 ? argv[1] : "config/access_gateway.yaml";
    auto cfg = kd39::common::config::LoadGatewayConfig(config_path);
    kd39::common::log::InitLogger({
        kServiceName,
        cfg.log_dir,
        static_cast<std::size_t>(cfg.log_max_size_mb),
        static_cast<std::size_t>(cfg.log_max_files),
        cfg.log_level,
        true,
    });
    KD39_LOG_INFO(
        "gateway runtime flags: http_io_threads={} ws_io_threads={} cobalt_exp={} asio_grpc_exp={} grpc_timeout_ms={} retry_attempts={} retry_backoff_ms={}",
        cfg.http_io_threads,
        cfg.ws_io_threads,
        cfg.enable_cobalt_experimental ? "on" : "off",
        cfg.enable_asio_grpc_experimental ? "on" : "off",
        cfg.grpc_timeout_ms,
        cfg.grpc_retry_attempts,
        cfg.grpc_retry_backoff_ms);

    kd39::framework::Application app(kServiceName);
    auto resolver = kd39::infrastructure::coordination::etcd::CreateServiceResolver(cfg.etcd_endpoints);
    auto router = std::make_shared<kd39::gateways::access::GrpcRouter>(
        kd39::gateways::access::RouterTargets{cfg.config_service_target, cfg.user_service_target, cfg.game_service_target},
        resolver,
        kd39::gateways::access::RouterRuntimeConfig{cfg.grpc_timeout_ms, cfg.grpc_retry_attempts, cfg.grpc_retry_backoff_ms});
    auto auth = std::make_shared<kd39::gateways::access::AuthMiddleware>(
        kd39::gateways::access::AuthOptions{
            cfg.jwt_secret,
            cfg.jwt_issuer,
            cfg.jwt_audience,
            cfg.allow_legacy_token,
        });

    const auto cobalt_experimental = cfg.enable_cobalt_experimental;
    kd39::gateways::access::HttpServer http(
        cfg.bind_host, cfg.http_port, router, auth,
        kd39::gateways::access::ServerRuntimeOptions{
            cfg.http_io_threads,
            cobalt_experimental,
        });
    kd39::gateways::access::WsServer ws(
        cfg.bind_host, cfg.ws_port, router, auth,
        kd39::gateways::access::ServerRuntimeOptions{
            cfg.ws_io_threads,
            cobalt_experimental,
        });

    http.Start();
    ws.Start();

    app.AddShutdownHook([&] {
        ws.Stop();
        http.Stop();
    });
    app.Run();
    return 0;
}
