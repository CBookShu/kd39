#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace kd39::infrastructure::coordination {

struct ServiceInstance {
    std::string service_name;
    std::string instance_id;
    std::string host;
    uint16_t    port = 0;
    std::string protocol;
    std::string version;
    std::string zone;
};

class ServiceRegistry {
public:
    virtual ~ServiceRegistry() = default;

    virtual bool Register(const ServiceInstance& inst, int ttl_seconds) = 0;
    virtual bool Deregister(const std::string& service_name,
                            const std::string& instance_id) = 0;
};

class ServiceResolver {
public:
    virtual ~ServiceResolver() = default;

    using Callback = std::function<void(const std::vector<ServiceInstance>&)>;

    virtual std::vector<ServiceInstance> Resolve(
        const std::string& service_name) = 0;

    virtual void Watch(const std::string& service_name, Callback cb) = 0;
};

}  // namespace kd39::infrastructure::coordination
