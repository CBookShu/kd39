#include "infrastructure/coordination/impl/etcd/factory.h"

#include "infrastructure/coordination/impl/etcd/etcd_state.h"
#include <spdlog/spdlog.h>

namespace kd39::infrastructure::coordination::etcd {

class EtcdConfigProvider final : public ConfigProvider {
public:
    explicit EtcdConfigProvider(std::string endpoints)
        : endpoints_(std::move(endpoints)) {
        spdlog::info("EtcdConfigProvider ready: {}", endpoints_);
    }

    std::string Get(const std::string& key) override {
        auto state = detail::GetState(endpoints_);
        std::scoped_lock lock(state->mu);
        if (auto it = state->kv.find(key); it != state->kv.end()) {
            return it->second;
        }
        return {};
    }

    void Watch(const std::string& prefix, WatchCallback cb) override {
        auto state = detail::GetState(endpoints_);
        std::scoped_lock lock(state->mu);
        state->config_watchers.emplace_back(prefix, cb);
        for (const auto& [key, value] : state->kv) {
            if (key.rfind(prefix, 0) == 0) {
                cb(key, value);
            }
        }
    }

    void Put(const std::string& key, const std::string& value) override {
        auto state = detail::GetState(endpoints_);
        std::scoped_lock lock(state->mu);
        state->kv[key] = value;
        detail::NotifyConfigWatchers(*state, key, value);
    }

private:
    std::string endpoints_;
};

std::shared_ptr<ConfigProvider> CreateConfigProvider(const std::string& endpoints) {
    return std::make_shared<EtcdConfigProvider>(endpoints);
}

}  // namespace kd39::infrastructure::coordination::etcd
