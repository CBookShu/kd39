#include "config_service_impl.h"

#include <nlohmann/json.hpp>

#include "common/log/logger.h"

namespace kd39::services::config {
namespace {
using kd39::api::config::ConfigEntry;
using kd39::infrastructure::storage::mysql::QueryRow;

std::string DefaultEnv(std::string env) {
    if (env.empty()) {
        env = "dev";
    }
    return env;
}
}  // namespace

ConfigServiceImpl::ConfigServiceImpl(Deps deps)
    : deps_(std::move(deps)) {
    KD39_LOG_INFO("ConfigServiceImpl created");
}

std::string ConfigServiceImpl::BuildStoreKey(const std::string& ns,
                                             const std::string& key,
                                             const std::string& env) {
    return env + "|" + ns + "|" + key;
}

std::string ConfigServiceImpl::BuildCacheKey(const std::string& ns,
                                             const std::string& key,
                                             const std::string& env) {
    return "config:" + env + ":" + ns + ":" + key;
}

std::string ConfigServiceImpl::BuildEtcdKey(const std::string& ns,
                                            const std::string& key,
                                            const std::string& env) {
    return "/config/" + env + "/" + ns + "/" + key;
}

std::string ConfigServiceImpl::SerializeEntry(const ConfigEntry& entry) {
    nlohmann::json json = {
        {"namespace_name", entry.namespace_name()},
        {"key", entry.key()},
        {"value", entry.value()},
        {"version", entry.version()},
        {"environment", entry.environment()},
    };
    return json.dump();
}

bool ConfigServiceImpl::DeserializeEntry(const std::string& payload, ConfigEntry* entry) {
    const auto json = nlohmann::json::parse(payload, nullptr, false);
    if (json.is_discarded()) {
        return false;
    }
    entry->set_namespace_name(json.value("namespace_name", ""));
    entry->set_key(json.value("key", ""));
    entry->set_value(json.value("value", ""));
    entry->set_version(json.value("version", 0LL));
    entry->set_environment(json.value("environment", "dev"));
    return true;
}

QueryRow ConfigServiceImpl::ToMysqlRow(const ConfigEntry& entry) {
    return {
        {"namespace_name", entry.namespace_name()},
        {"key", entry.key()},
        {"value", entry.value()},
        {"version", std::to_string(entry.version())},
        {"environment", entry.environment()},
    };
}

ConfigEntry ConfigServiceImpl::FromMysqlRow(const QueryRow& row) {
    ConfigEntry entry;
    if (auto it = row.find("namespace_name"); it != row.end()) entry.set_namespace_name(it->second);
    if (auto it = row.find("key"); it != row.end()) entry.set_key(it->second);
    if (auto it = row.find("value"); it != row.end()) entry.set_value(it->second);
    if (auto it = row.find("version"); it != row.end()) entry.set_version(std::stoll(it->second));
    if (auto it = row.find("environment"); it != row.end()) entry.set_environment(it->second);
    return entry;
}

std::optional<ConfigServiceImpl::ConfigEntry> ConfigServiceImpl::FindEntry(const std::string& ns,
                                                                           const std::string& key,
                                                                           const std::string& env) {
    const auto env_name = DefaultEnv(env);

    if (deps_.redis) {
        if (auto cached = deps_.redis->Get(BuildCacheKey(ns, key, env_name))) {
            ConfigEntry entry;
            if (DeserializeEntry(*cached, &entry)) {
                return entry;
            }
        }
    }

    if (deps_.mysql) {
        const auto rows = deps_.mysql->FindRows("config_entries", {
            {"namespace_name", ns},
            {"key", key},
            {"environment", env_name},
        });
        if (!rows.empty()) {
            auto entry = FromMysqlRow(rows.front());
            if (deps_.redis) {
                deps_.redis->Set(BuildCacheKey(ns, key, env_name), SerializeEntry(entry));
            }
            std::scoped_lock lock(mu_);
            entries_[BuildStoreKey(ns, key, env_name)] = entry;
            return entry;
        }
    }

    std::scoped_lock lock(mu_);
    auto it = entries_.find(BuildStoreKey(ns, key, env_name));
    if (it != entries_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<ConfigServiceImpl::ConfigEntry> ConfigServiceImpl::CollectNamespace(const std::string& ns,
                                                                                std::int64_t since_version) {
    std::vector<ConfigEntry> out;
    std::scoped_lock lock(mu_);
    for (const auto& [_, entry] : entries_) {
        if (entry.namespace_name() == ns && entry.version() > since_version) {
            out.push_back(entry);
        }
    }
    return out;
}

grpc::Status ConfigServiceImpl::GetConfig(grpc::ServerContext*,
                                          const kd39::api::config::GetConfigRequest* request,
                                          kd39::api::config::GetConfigResponse* response) {
    auto entry = FindEntry(request->namespace_name(), request->key(), "dev");
    if (!entry.has_value()) {
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "config not found");
    }
    *response->mutable_entry() = *entry;
    return grpc::Status::OK;
}

grpc::Status ConfigServiceImpl::BatchGetConfig(grpc::ServerContext*,
                                               const kd39::api::config::BatchGetConfigRequest* request,
                                               kd39::api::config::BatchGetConfigResponse* response) {
    for (const auto& entry : CollectNamespace(request->namespace_name(), -1)) {
        *response->add_entries() = entry;
    }
    return grpc::Status::OK;
}

grpc::Status ConfigServiceImpl::PublishConfig(grpc::ServerContext*,
                                              const kd39::api::config::PublishConfigRequest* request,
                                              kd39::api::config::PublishConfigResponse* response) {
    auto entry = request->entry();
    if (entry.namespace_name().empty() || entry.key().empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "namespace and key are required");
    }

    entry.set_environment(DefaultEnv(entry.environment()));
    entry.set_version(next_version_.fetch_add(1));

    {
        std::scoped_lock lock(mu_);
        entries_[BuildStoreKey(entry.namespace_name(), entry.key(), entry.environment())] = entry;
    }

    if (deps_.mysql) {
        deps_.mysql->UpsertRow("config_entries", ToMysqlRow(entry), {"namespace_name", "key", "environment"});
    }
    if (deps_.redis) {
        deps_.redis->Set(BuildCacheKey(entry.namespace_name(), entry.key(), entry.environment()), SerializeEntry(entry));
    }
    if (deps_.config_provider) {
        deps_.config_provider->Put(BuildEtcdKey(entry.namespace_name(), entry.key(), entry.environment()), entry.value());
    }
    if (deps_.mq_producer) {
        deps_.mq_producer->Publish("config.changed", SerializeEntry(entry));
    }

    response->set_version(entry.version());
    return grpc::Status::OK;
}

grpc::Status ConfigServiceImpl::WatchConfig(grpc::ServerContext*,
                                            const kd39::api::config::WatchConfigRequest* request,
                                            grpc::ServerWriter<kd39::api::config::WatchConfigResponse>* writer) {
    const auto updates = CollectNamespace(request->namespace_name(), request->since_version());
    if (!updates.empty()) {
        kd39::api::config::WatchConfigResponse response;
        for (const auto& entry : updates) {
            *response.add_updates() = entry;
        }
        writer->Write(response);
    }
    return grpc::Status::OK;
}

}  // namespace kd39::services::config
