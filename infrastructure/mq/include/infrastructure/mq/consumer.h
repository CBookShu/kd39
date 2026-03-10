#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace kd39::infrastructure::mq {

using MessageHandler = std::function<void(std::string_view payload)>;

class Consumer {
public:
    virtual ~Consumer() = default;
    virtual void Subscribe(std::string_view topic, MessageHandler handler) = 0;
    virtual void Start() = 0;
    virtual void Stop() = 0;
};

std::shared_ptr<Consumer> CreateRedisStreamsConsumer(
    const std::string& redis_uri,
    const std::string& group,
    const std::string& consumer_name);

}  // namespace kd39::infrastructure::mq
