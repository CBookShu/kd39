#pragma once

#include <string>
#include <vector>
#include "infrastructure/coordination/service_registry.h"

namespace kd39::framework::governance {

class TrafficRouter {
public:
    kd39::infrastructure::coordination::ServiceInstance Select(
        const std::vector<kd39::infrastructure::coordination::ServiceInstance>& instances,
        const std::string& traffic_tag) const;
};

}  // namespace kd39::framework::governance
