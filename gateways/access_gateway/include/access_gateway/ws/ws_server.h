#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "access_gateway/auth/auth_middleware.h"
#include "access_gateway/routing/grpc_router.h"

namespace kd39::gateways::access {

class WsServer {
public:
    WsServer(const std::string& address,
             uint16_t port,
             std::shared_ptr<GrpcRouter> router,
             std::shared_ptr<AuthMiddleware> auth);
    ~WsServer();

    void Start();
    void Stop();

    std::string HandleMessageForTesting(const std::string& payload,
                                        const std::string& auth_token) const;

private:
    std::string address_;
    uint16_t port_;
    std::shared_ptr<GrpcRouter> router_;
    std::shared_ptr<AuthMiddleware> auth_;
};

}  // namespace kd39::gateways::access
