#include "infrastructure/mq/consumer.h"

#include "local_bus.h"
#include <atomic>
#include <chrono>
#include <spdlog/spdlog.h>
#include <thread>
#include <unordered_map>

namespace kd39::infrastructure::mq {

class RedisStreamsConsumer final : public Consumer {
public:
    RedisStreamsConsumer(std::string uri, std::string group, std::string name)
        : uri_(std::move(uri)), group_(std::move(group)), name_(std::move(name)) {
        spdlog::info("RedisStreamsConsumer created: {} group={} consumer={}", uri_, group_, name_);
    }

    ~RedisStreamsConsumer() override { Stop(); }

    void Subscribe(std::string_view topic, MessageHandler handler) override {
        subscriptions_[std::string(topic)] = std::move(handler);
        spdlog::info("Subscribed topic={}", topic);
    }

    void Start() override {
        if (running_.exchange(true)) {
            return;
        }
        worker_ = std::thread([this] {
            while (running_.load()) {
                bool handled = false;
                for (auto& [topic, handler] : subscriptions_) {
                    for (auto& payload : detail::GetBus().Pop(topic, 32)) {
                        handler(payload);
                        handled = true;
                    }
                }
                if (!handled) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }
        });
    }

    void Stop() override {
        if (!running_.exchange(false)) {
            return;
        }
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    std::string uri_;
    std::string group_;
    std::string name_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::unordered_map<std::string, MessageHandler> subscriptions_;
};

std::shared_ptr<Consumer> CreateRedisStreamsConsumer(
    const std::string& redis_uri,
    const std::string& group,
    const std::string& consumer_name) {
    return std::make_shared<RedisStreamsConsumer>(redis_uri, group, consumer_name);
}

}  // namespace kd39::infrastructure::mq
