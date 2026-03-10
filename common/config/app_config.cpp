#include "common/config/app_config.h"

#include <fstream>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_map>

namespace kd39::common::config {
namespace {
using FlatConfig = std::unordered_map<std::string, std::string>;

std::string Trim(std::string value) {
    const auto is_space = [](unsigned char ch) {
        return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
    };
    while (!value.empty() && is_space(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

FlatConfig LoadFlatConfig(const std::string& path) {
    FlatConfig cfg;
    std::ifstream in(path);
    if (!in.is_open()) {
        spdlog::warn("config file '{}' not found, using defaults", path);
        return cfg;
    }

    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line.starts_with('#')) {
            continue;
        }
        const auto pos = line.find(':');
        if (pos == std::string::npos) {
            continue;
        }
        auto key = Trim(line.substr(0, pos));
        auto value = Trim(line.substr(pos + 1));
        if (!value.empty() && ((value.front() == '"' && value.back() == '"') ||
                               (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }
        cfg[key] = value;
    }
    return cfg;
}

template <typename T>
T GetOr(const FlatConfig& cfg, const std::string& key, T fallback);

template <>
inline std::string GetOr(const FlatConfig& cfg, const std::string& key, std::string fallback) {
    if (auto it = cfg.find(key); it != cfg.end()) {
        return it->second;
    }
    return fallback;
}

template <>
inline uint16_t GetOr(const FlatConfig& cfg, const std::string& key, uint16_t fallback) {
    if (auto it = cfg.find(key); it != cfg.end()) {
        return static_cast<uint16_t>(std::stoi(it->second));
    }
    return fallback;
}

template <>
inline int GetOr(const FlatConfig& cfg, const std::string& key, int fallback) {
    if (auto it = cfg.find(key); it != cfg.end()) {
        return std::stoi(it->second);
    }
    return fallback;
}

}  // namespace

ServiceConfig LoadServiceConfig(const std::string& path,
                                const std::string& default_service_name,
                                uint16_t default_grpc_port) {
    const auto cfg = LoadFlatConfig(path);
    ServiceConfig service;
    service.service_name = GetOr(cfg, "service_name", default_service_name);
    service.bind_host = GetOr(cfg, "bind_host", std::string("0.0.0.0"));
    service.grpc_port = GetOr(cfg, "grpc_port", default_grpc_port);
    service.mysql_host = GetOr(cfg, "mysql_host", std::string("127.0.0.1"));
    service.mysql_port = GetOr(cfg, "mysql_port", static_cast<uint16_t>(3306));
    service.mysql_user = GetOr(cfg, "mysql_user", std::string("root"));
    service.mysql_password = GetOr(cfg, "mysql_password", std::string("kd39dev"));
    service.mysql_db = GetOr(cfg, "mysql_db", std::string("kd39"));
    service.mysql_pool_size = GetOr(cfg, "mysql_pool_size", 4);
    service.redis_host = GetOr(cfg, "redis_host", std::string("127.0.0.1"));
    service.redis_port = GetOr(cfg, "redis_port", static_cast<uint16_t>(6379));
    service.redis_password = GetOr(cfg, "redis_password", std::string());
    service.redis_db = GetOr(cfg, "redis_db", 0);
    service.redis_pool_size = GetOr(cfg, "redis_pool_size", 4);
    service.etcd_endpoints = GetOr(cfg, "etcd_endpoints", std::string("http://127.0.0.1:2379"));
    service.mq_uri = GetOr(cfg, "mq_uri", std::string("redis://127.0.0.1:6379"));
    service.registry_ttl_seconds = GetOr(cfg, "registry_ttl_seconds", 30);
    return service;
}

GatewayConfig LoadGatewayConfig(const std::string& path) {
    const auto cfg = LoadFlatConfig(path);
    GatewayConfig gateway;
    gateway.bind_host = GetOr(cfg, "bind_host", std::string("0.0.0.0"));
    gateway.http_port = GetOr(cfg, "http_port", static_cast<uint16_t>(8080));
    gateway.ws_port = GetOr(cfg, "ws_port", static_cast<uint16_t>(8081));
    gateway.etcd_endpoints = GetOr(cfg, "etcd_endpoints", std::string("http://127.0.0.1:2379"));
    gateway.config_service_target = GetOr(cfg, "config_service_target", std::string("127.0.0.1:50051"));
    gateway.user_service_target = GetOr(cfg, "user_service_target", std::string("127.0.0.1:50052"));
    gateway.game_service_target = GetOr(cfg, "game_service_target", std::string("127.0.0.1:50053"));
    gateway.jwt_secret = GetOr(cfg, "jwt_secret", std::string("dev-secret"));
    return gateway;
}

}  // namespace kd39::common::config
