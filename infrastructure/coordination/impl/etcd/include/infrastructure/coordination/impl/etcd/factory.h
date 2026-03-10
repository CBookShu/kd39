#pragma once

#include <memory>
#include <string>

#include "infrastructure/coordination/config_watcher.h"
#include "infrastructure/coordination/distributed_lock.h"
#include "infrastructure/coordination/leader_election.h"
#include "infrastructure/coordination/service_registry.h"

namespace kd39::infrastructure::coordination::etcd {

std::shared_ptr<ServiceRegistry> CreateServiceRegistry(const std::string& endpoints);
std::shared_ptr<ServiceResolver> CreateServiceResolver(const std::string& endpoints);
std::shared_ptr<ConfigProvider> CreateConfigProvider(const std::string& endpoints);
std::shared_ptr<DistributedLock> CreateDistributedLock(const std::string& endpoints);
std::shared_ptr<LeaderElection> CreateLeaderElection(const std::string& endpoints);

}  // namespace kd39::infrastructure::coordination::etcd
