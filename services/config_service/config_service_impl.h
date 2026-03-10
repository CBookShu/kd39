#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "config/config_service.grpc.pb.h"
#include "infrastructure/coordination/config_watcher.h"
#include "infrastructure/mq/producer.h"
#include "infrastructure/storage/mysql/connection_pool.h"
#include "infrastructure/storage/redis/redis_client.h"

namespace kd39::services::config {

class ConfigServiceImpl final : public kd39::api::config::ConfigService::Service {
public:
    struct Deps {
        std::shared_ptr<infrastructure::coordination::ConfigProvider> config_provider;
        std::shared_ptr<infrastructure::storage::mysql::ConnectionPool> mysql;
        std::shared_ptr<infrastructure::storage::redis::RedisClient> redis;
        std::shared_ptr<infrastructure::mq::Producer> mq_producer;
    };

    explicit ConfigServiceImpl(Deps deps);

    grpc::Status GetConfig(grpc::ServerContext* context,
                           const kd39::api::config::GetConfigRequest* request,
                           kd39::api::config::GetConfigResponse* response) override;
    grpc::Status BatchGetConfig(grpc::ServerContext* context,
                                const kd39::api::config::BatchGetConfigRequest* request,
                                kd39::api::config::BatchGetConfigResponse* response) override;
    grpc::Status PublishConfig(grpc::ServerContext* context,
                               const kd39::api::config::PublishConfigRequest* request,
                               kd39::api::config::PublishConfigResponse* response) override;
    grpc::Status WatchConfig(grpc::ServerContext* context,
                             const kd39::api::config::WatchConfigRequest* request,
                             grpc::ServerWriter<kd39::api::config::WatchConfigResponse>* writer) override;

private:
    using ConfigEntry = kd39::api::config::ConfigEntry;

    static std::string BuildStoreKey(const std::string& ns,
                                     const std::string& key,
                                     const std::string& env);
    static std::string BuildCacheKey(const std::string& ns,
                                     const std::string& key,
                                     const std::string& env);
    static std::string BuildEtcdKey(const std::string& ns,
                                    const std::string& key,
                                    const std::string& env);
    static std::string SerializeEntry(const ConfigEntry& entry);
    static bool DeserializeEntry(const std::string& payload, ConfigEntry* entry);
    static infrastructure::storage::mysql::QueryRow ToMysqlRow(const ConfigEntry& entry);
    static ConfigEntry FromMysqlRow(const infrastructure::storage::mysql::QueryRow& row);

    std::optional<ConfigEntry> FindEntry(const std::string& ns,
                                         const std::string& key,
                                         const std::string& env);
    std::vector<ConfigEntry> CollectNamespace(const std::string& ns,
                                              std::int64_t since_version);

    Deps deps_;
    std::mutex mu_;
    std::unordered_map<std::string, ConfigEntry> entries_;
    std::atomic<long long> next_version_{1};
};

}  // namespace kd39::services::config
