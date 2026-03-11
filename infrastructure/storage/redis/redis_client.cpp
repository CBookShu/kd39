#include "infrastructure/storage/redis/redis_client.h"

#include <chrono>
#include <deque>
#include <mutex>
#include <unordered_map>

#include "common/log/logger.h"

namespace kd39::infrastructure::storage::redis {
namespace {
struct ValueEntry {
    std::string value;
    std::optional<std::chrono::steady_clock::time_point> expires_at;
};

struct StreamMessage {
    std::string id;
    std::string payload;
};

struct BackendState {
    std::mutex mu;
    std::unordered_map<std::string, ValueEntry> kv;
    std::unordered_map<std::string, std::deque<StreamMessage>> streams;
    std::uint64_t next_stream_id = 1;
};

std::shared_ptr<BackendState> SharedStateFor(const RedisConfig& cfg) {
    static std::mutex global_mu;
    static std::unordered_map<std::string, std::weak_ptr<BackendState>> states;
    const auto key = cfg.host + ":" + std::to_string(cfg.port) + "/" + std::to_string(cfg.db);
    std::scoped_lock lock(global_mu);
    if (auto existing = states[key].lock()) {
        return existing;
    }
    auto created = std::make_shared<BackendState>();
    states[key] = created;
    return created;
}

void PurgeExpired(BackendState& state, const std::string& key) {
    auto it = state.kv.find(key);
    if (it == state.kv.end() || !it->second.expires_at.has_value()) {
        return;
    }
    if (std::chrono::steady_clock::now() >= *it->second.expires_at) {
        state.kv.erase(it);
    }
}
}  // namespace

class RedisClientImpl final : public RedisClient {
public:
    explicit RedisClientImpl(RedisConfig cfg)
        : cfg_(std::move(cfg)), state_(SharedStateFor(cfg_)) {
        KD39_LOG_INFO("Redis client ready: {}:{} db={}", cfg_.host, cfg_.port, cfg_.db);
    }

    bool Set(const std::string& key, const std::string& value) override {
        std::scoped_lock lock(state_->mu);
        state_->kv[key] = ValueEntry{value, std::nullopt};
        return true;
    }

    std::optional<std::string> Get(const std::string& key) override {
        std::scoped_lock lock(state_->mu);
        PurgeExpired(*state_, key);
        if (auto it = state_->kv.find(key); it != state_->kv.end()) {
            return it->second.value;
        }
        return std::nullopt;
    }

    bool Del(const std::string& key) override {
        std::scoped_lock lock(state_->mu);
        return state_->kv.erase(key) > 0;
    }

    bool Expire(const std::string& key, int ttl_seconds) override {
        std::scoped_lock lock(state_->mu);
        if (auto it = state_->kv.find(key); it != state_->kv.end()) {
            it->second.expires_at = std::chrono::steady_clock::now() + std::chrono::seconds(ttl_seconds);
            return true;
        }
        return false;
    }

    std::string XAdd(const std::string& stream, const std::string& payload) override {
        std::scoped_lock lock(state_->mu);
        const auto id = std::to_string(state_->next_stream_id++) + "-0";
        state_->streams[stream].push_back(StreamMessage{id, payload});
        return id;
    }

    std::vector<std::string> XRead(const std::string& stream, std::size_t max_count) override {
        std::vector<std::string> result;
        std::scoped_lock lock(state_->mu);
        auto& queue = state_->streams[stream];
        while (!queue.empty() && result.size() < max_count) {
            result.push_back(queue.front().payload);
            queue.pop_front();
        }
        return result;
    }

private:
    RedisConfig cfg_;
    std::shared_ptr<BackendState> state_;
};

std::shared_ptr<RedisClient> RedisClient::Create(const RedisConfig& cfg) {
    return std::make_shared<RedisClientImpl>(cfg);
}

}  // namespace kd39::infrastructure::storage::redis
