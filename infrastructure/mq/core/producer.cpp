#include "infrastructure/mq/producer.h"

#include "local_bus.h"
#include <spdlog/spdlog.h>

namespace kd39::infrastructure::mq {

class RedisStreamsProducer final : public Producer {
public:
    explicit RedisStreamsProducer(std::string uri) : uri_(std::move(uri)) {
        spdlog::info("RedisStreamsProducer created: {}", uri_);
    }

    bool Publish(std::string_view topic, std::string_view payload) override {
        detail::GetBus().Publish(std::string(topic), std::string(payload));
        spdlog::debug("Published topic={} bytes={}", topic, payload.size());
        return true;
    }

private:
    std::string uri_;
};

std::shared_ptr<Producer> CreateRedisStreamsProducer(const std::string& redis_uri) {
    return std::make_shared<RedisStreamsProducer>(redis_uri);
}

}  // namespace kd39::infrastructure::mq
