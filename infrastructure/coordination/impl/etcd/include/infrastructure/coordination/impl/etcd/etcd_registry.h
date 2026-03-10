#pragma once

#include "infrastructure/coordination/service_registry.h"
#include <string>

namespace kd39::infrastructure::coordination::etcd {

class EtcdServiceRegistry : public ServiceRegistry {
public:
    explicit EtcdServiceRegistry(const std::string& endpoints);
    ~EtcdServiceRegistry() override;

    bool Register(const ServiceInstance& inst, int ttl_seconds) override;
    bool Deregister(const std::string& service_name,
                    const std::string& instance_id) override;

private:
    std::string endpoints_;
};

class EtcdServiceResolver : public ServiceResolver {
public:
    explicit EtcdServiceResolver(const std::string& endpoints);
    ~EtcdServiceResolver() override;

    std::vector<ServiceInstance> Resolve(const std::string& service_name) override;
    void Watch(const std::string& service_name, Callback cb) override;

private:
    std::string endpoints_;
};

}  // namespace kd39::infrastructure::coordination::etcd
