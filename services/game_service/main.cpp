#include "game_service_impl.h"

#include <fmt/format.h>
#include <grpcpp/grpcpp.h>

#include "common/config/app_config.h"
#include "common/log/logger.h"
#include "infrastructure/coordination/impl/etcd/factory.h"
#include "infrastructure/storage/redis/redis_client.h"

int main(int argc, char* argv[]) {
    const std::string config_path = argc > 1 ? argv[1] : "config/game_service.yaml";
    auto cfg = kd39::common::config::LoadServiceConfig(config_path, "game_service", 50053);
    kd39::common::log::InitLogger(cfg.service_name);

    auto redis = kd39::infrastructure::storage::redis::RedisClient::Create({
        cfg.redis_host, cfg.redis_port, cfg.redis_password, cfg.redis_db, cfg.redis_pool_size
    });
    auto registry = kd39::infrastructure::coordination::etcd::CreateServiceRegistry(cfg.etcd_endpoints);

    kd39::services::game::GameServiceImpl service({redis});

    grpc::ServerBuilder builder;
    const auto listen_addr = fmt::format("{}:{}", cfg.bind_host, cfg.grpc_port);
    builder.AddListeningPort(listen_addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    auto server = builder.BuildAndStart();
    if (!server) {
        return 1;
    }

    registry->Register({cfg.service_name,
                        fmt::format("{}-{}", cfg.service_name, cfg.grpc_port),
                        cfg.bind_host,
                        cfg.grpc_port,
                        "grpc",
                        "v1",
                        "default"},
                       cfg.registry_ttl_seconds);

    server->Wait();
    return 0;
}
