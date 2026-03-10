#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "infrastructure/coordination/config_watcher.h"
#include "infrastructure/coordination/service_registry.h"

namespace kd39::infrastructure::coordination::etcd::detail {

struct SharedEtcdState {
    std::mutex mu;
    std::unordered_map<std::string, std::string> kv;
    std::unordered_map<std::string, ServiceInstance> services;
    std::vector<std::pair<std::string, ConfigProvider::WatchCallback>> config_watchers;
    std::vector<std::pair<std::string, ServiceResolver::Callback>> service_watchers;
    std::unordered_map<std::string, std::string> locks;
    std::unordered_map<std::string, std::string> leaders;
};

inline std::shared_ptr<SharedEtcdState> GetState(const std::string& endpoints) {
    static std::mutex global_mu;
    static std::unordered_map<std::string, std::weak_ptr<SharedEtcdState>> states;
    std::scoped_lock lock(global_mu);
    if (auto existing = states[endpoints].lock()) {
        return existing;
    }
    auto created = std::make_shared<SharedEtcdState>();
    states[endpoints] = created;
    return created;
}

inline std::vector<ServiceInstance> SnapshotInstances(SharedEtcdState& state, const std::string& service_name) {
    std::vector<ServiceInstance> out;
    for (const auto& [_, instance] : state.services) {
        if (instance.service_name == service_name) {
            out.push_back(instance);
        }
    }
    return out;
}

inline void NotifyServiceWatchers(SharedEtcdState& state, const std::string& service_name) {
    auto snapshot = SnapshotInstances(state, service_name);
    for (const auto& [watched_service, cb] : state.service_watchers) {
        if (watched_service == service_name) {
            cb(snapshot);
        }
    }
}

inline void NotifyConfigWatchers(SharedEtcdState& state,
                                 const std::string& key,
                                 const std::string& value) {
    for (const auto& [prefix, cb] : state.config_watchers) {
        if (key.rfind(prefix, 0) == 0) {
            cb(key, value);
        }
    }
}

}  // namespace kd39::infrastructure::coordination::etcd::detail
