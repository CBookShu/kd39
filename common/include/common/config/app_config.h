#pragma once

#include <cstdint>
#include <string>

namespace kd39::common::config {

struct ServiceConfig {
    std::string service_name;
    std::string bind_host = "0.0.0.0";
    uint16_t grpc_port = 0;
    std::string log_dir = "logs";
    int log_max_size_mb = 100;
    int log_max_files = 10;
    std::string log_level = "info";

    std::string mysql_host = "127.0.0.1";
    uint16_t mysql_port = 3306;
    std::string mysql_user = "root";
    std::string mysql_password = "kd39dev";
    std::string mysql_db = "kd39";
    int mysql_pool_size = 4;

    std::string redis_host = "127.0.0.1";
    uint16_t redis_port = 6379;
    std::string redis_password;
    int redis_db = 0;
    int redis_pool_size = 4;

    std::string etcd_endpoints = "http://127.0.0.1:2379";
    std::string mq_uri = "redis://127.0.0.1:6379";
    int registry_ttl_seconds = 30;
};

struct GatewayConfig {
    std::string bind_host = "0.0.0.0";
    uint16_t http_port = 8080;
    int http_io_threads = 2;
    std::string log_dir = "logs";
    int log_max_size_mb = 100;
    int log_max_files = 10;
    std::string log_level = "info";
    std::string etcd_endpoints = "http://127.0.0.1:2379";
    std::string config_service_target = "127.0.0.1:50051";
    std::string user_service_target = "127.0.0.1:50052";
    std::string game_service_target = "127.0.0.1:50053";
    std::string jwt_secret = "dev-secret";
    std::string jwt_issuer;
    std::string jwt_audience;
    bool allow_legacy_token = true;
    int grpc_timeout_ms = 800;
    int grpc_retry_attempts = 2;
    int grpc_retry_backoff_ms = 50;
    bool enable_cobalt_experimental = false;
    bool enable_asio_grpc_experimental = false;
};

ServiceConfig LoadServiceConfig(const std::string& path,
                                const std::string& default_service_name,
                                uint16_t default_grpc_port);
GatewayConfig LoadGatewayConfig(const std::string& path);

}  // namespace kd39::common::config
