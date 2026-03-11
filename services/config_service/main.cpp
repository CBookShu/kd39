#include "config_service_impl.h"

#include <fmt/format.h>
#include <grpcpp/grpcpp.h>

#include "common/config/app_config.h"
#include "common/log/logger.h"
#include "infrastructure/coordination/impl/etcd/factory.h"
#include "infrastructure/mq/producer.h"
#include "infrastructure/storage/mysql/connection_pool.h"
#include "infrastructure/storage/redis/redis_client.h"

int main(int argc, char* argv[]) {
    const std::string config_path = argc > 1 ? argv[1] : "config/config_service.yaml";
    auto cfg = kd39::common::config::LoadServiceConfig(config_path, "config_service", 50051);
    kd39::common::log::InitLogger({
        cfg.service_name,
        cfg.log_dir,
        static_cast<std::size_t>(cfg.log_max_size_mb),
        static_cast<std::size_t>(cfg.log_max_files),
        cfg.log_level,
        true,
    });

    auto mysql = kd39::infrastructure::storage::mysql::ConnectionPool::Create({
        cfg.mysql_host, cfg.mysql_port, cfg.mysql_user, cfg.mysql_password, cfg.mysql_db, cfg.mysql_pool_size
    });
    auto redis = kd39::infrastructure::storage::redis::RedisClient::Create({
        cfg.redis_host, cfg.redis_port, cfg.redis_password, cfg.redis_db, cfg.redis_pool_size
    });
    auto provider = kd39::infrastructure::coordination::etcd::CreateConfigProvider(cfg.etcd_endpoints);
    auto producer = kd39::infrastructure::mq::CreateRedisStreamsProducer(cfg.mq_uri);
    auto registry = kd39::infrastructure::coordination::etcd::CreateServiceRegistry(cfg.etcd_endpoints);

    kd39::services::config::ConfigServiceImpl service({provider, mysql, redis, producer});

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
