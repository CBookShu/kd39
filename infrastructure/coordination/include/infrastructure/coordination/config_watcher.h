#pragma once

#include <functional>
#include <memory>
#include <string>

namespace kd39::infrastructure::coordination {

// ConfigProvider: abstraction for config sources (etcd, k8s configmap, etc.)
class ConfigProvider {
public:
    virtual ~ConfigProvider() = default;

    virtual std::string Get(const std::string& key) = 0;

    using WatchCallback = std::function<void(const std::string& key,
                                             const std::string& value)>;

    virtual void Watch(const std::string& prefix, WatchCallback cb) = 0;

    virtual void Put(const std::string& key, const std::string& value) = 0;
};

}  // namespace kd39::infrastructure::coordination
