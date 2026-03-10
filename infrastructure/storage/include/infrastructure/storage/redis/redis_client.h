#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kd39::infrastructure::storage::redis {

struct RedisConfig {
    std::string host = "127.0.0.1";
    uint16_t port = 6379;
    std::string password;
    int db = 0;
    int pool_size = 4;
};

class RedisClient {
public:
    virtual ~RedisClient() = default;

    static std::shared_ptr<RedisClient> Create(const RedisConfig& cfg);

    virtual bool Set(const std::string& key, const std::string& value) = 0;
    virtual std::optional<std::string> Get(const std::string& key) = 0;
    virtual bool Del(const std::string& key) = 0;
    virtual bool Expire(const std::string& key, int ttl_seconds) = 0;
    virtual std::string XAdd(const std::string& stream, const std::string& payload) = 0;
    virtual std::vector<std::string> XRead(const std::string& stream, std::size_t max_count) = 0;
};

}  // namespace kd39::infrastructure::storage::redis
