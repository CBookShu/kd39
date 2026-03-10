#include "framework/governance/traffic_router.h"

namespace kd39::framework::governance {

kd39::infrastructure::coordination::ServiceInstance TrafficRouter::Select(
    const std::vector<kd39::infrastructure::coordination::ServiceInstance>& instances,
    const std::string& /*traffic_tag*/) const {
    if (instances.empty()) {
        return {};
    }
    return instances.front();
}

}  // namespace kd39::framework::governance
