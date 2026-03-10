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
    const std::string config_path = argc > 1 ? argv[1] : "config/access_gateway.yaml";
    auto cfg = kd39::common::config::LoadGatewayConfig(config_path);
    kd39::common::log::InitLogger("access_gateway");

    kd39::framework::Application app("access_gateway");
    auto resolver = kd39::infrastructure::coordination::etcd::CreateServiceResolver(cfg.etcd_endpoints);
    auto router = std::make_shared<kd39::gateways::access::GrpcRouter>(
        kd39::gateways::access::RouterTargets{cfg.config_service_target, cfg.user_service_target, cfg.game_service_target}, resolver);
    auto auth = std::make_shared<kd39::gateways::access::AuthMiddleware>();

    kd39::gateways::access::HttpServer http(cfg.bind_host, cfg.http_port, router, auth);
    kd39::gateways::access::WsServer ws(cfg.bind_host, cfg.ws_port, router, auth);

    http.Start();
    ws.Start();

    app.AddShutdownHook([&] {
        ws.Stop();
        http.Stop();
    });
    app.Run();
    return 0;
}
