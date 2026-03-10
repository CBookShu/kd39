#pragma once

#include <memory>
#include <string>

#include "common/context/request_context.h"
#include "infrastructure/coordination/service_registry.h"

namespace kd39::gateways::access {

struct RouterTargets {
    std::string config_service_target = "127.0.0.1:50051";
    std::string user_service_target = "127.0.0.1:50052";
    std::string game_service_target = "127.0.0.1:50053";
};

class GrpcRouter {
public:
    explicit GrpcRouter(RouterTargets targets,
                        std::shared_ptr<infrastructure::coordination::ServiceResolver> resolver = nullptr);

    std::string Route(const std::string& path,
                      const std::string& body,
                      const common::RequestContext& ctx);

private:
    std::string ResolveTarget(const std::string& service_name,
                              const std::string& fallback,
                              const std::string& traffic_tag) const;

    RouterTargets targets_;
    std::shared_ptr<infrastructure::coordination::ServiceResolver> resolver_;
};

}  // namespace kd39::gateways::access
