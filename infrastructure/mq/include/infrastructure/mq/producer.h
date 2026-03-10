#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace kd39::infrastructure::mq {

class Producer {
public:
    virtual ~Producer() = default;
    virtual bool Publish(std::string_view topic, std::string_view payload) = 0;
};

std::shared_ptr<Producer> CreateRedisStreamsProducer(const std::string& redis_uri);

}  // namespace kd39::infrastructure::mq
