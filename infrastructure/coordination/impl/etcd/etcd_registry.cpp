#include "infrastructure/coordination/impl/etcd/etcd_registry.h"

#include "infrastructure/coordination/impl/etcd/etcd_state.h"
#include "infrastructure/coordination/impl/etcd/factory.h"
#include <spdlog/spdlog.h>

namespace kd39::infrastructure::coordination::etcd {

EtcdServiceRegistry::EtcdServiceRegistry(const std::string& endpoints)
    : endpoints_(endpoints) {
    spdlog::info("EtcdServiceRegistry ready: {}", endpoints_);
}

EtcdServiceRegistry::~EtcdServiceRegistry() = default;

bool EtcdServiceRegistry::Register(const ServiceInstance& inst, int ttl_seconds) {
    auto state = detail::GetState(endpoints_);
    std::scoped_lock lock(state->mu);
    state->services[inst.service_name + "/" + inst.instance_id] = inst;
    detail::NotifyServiceWatchers(*state, inst.service_name);
    spdlog::info("Registered service {}/{} ttl={}s", inst.service_name, inst.instance_id, ttl_seconds);
    return true;
}

bool EtcdServiceRegistry::Deregister(const std::string& service_name,
                                     const std::string& instance_id) {
    auto state = detail::GetState(endpoints_);
    std::scoped_lock lock(state->mu);
    state->services.erase(service_name + "/" + instance_id);
    detail::NotifyServiceWatchers(*state, service_name);
    spdlog::info("Deregistered service {}/{}", service_name, instance_id);
    return true;
}

EtcdServiceResolver::EtcdServiceResolver(const std::string& endpoints)
    : endpoints_(endpoints) {
    spdlog::info("EtcdServiceResolver ready: {}", endpoints_);
}

EtcdServiceResolver::~EtcdServiceResolver() = default;

std::vector<ServiceInstance> EtcdServiceResolver::Resolve(const std::string& service_name) {
    auto state = detail::GetState(endpoints_);
    std::scoped_lock lock(state->mu);
    return detail::SnapshotInstances(*state, service_name);
}

void EtcdServiceResolver::Watch(const std::string& service_name, Callback cb) {
    auto state = detail::GetState(endpoints_);
    std::scoped_lock lock(state->mu);
    state->service_watchers.emplace_back(service_name, cb);
    cb(detail::SnapshotInstances(*state, service_name));
}

std::shared_ptr<ServiceRegistry> CreateServiceRegistry(const std::string& endpoints) {
    return std::make_shared<EtcdServiceRegistry>(endpoints);
}

std::shared_ptr<ServiceResolver> CreateServiceResolver(const std::string& endpoints) {
    return std::make_shared<EtcdServiceResolver>(endpoints);
}

}  // namespace kd39::infrastructure::coordination::etcd
