#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "access_gateway/auth/auth_middleware.h"
#include "access_gateway/routing/grpc_router.h"

namespace kd39::gateways::access {

class HttpServer {
public:
    HttpServer(const std::string& address,
               uint16_t port,
               std::shared_ptr<GrpcRouter> router,
               std::shared_ptr<AuthMiddleware> auth);
    ~HttpServer();

    void Start();
    void Stop();

    std::string HandleRequestForTesting(const std::string& path,
                                        const std::string& body,
                                        const std::string& auth_token,
                                        const std::unordered_map<std::string, std::string>& headers = {}) const;

private:
    std::string address_;
    uint16_t port_;
    std::shared_ptr<GrpcRouter> router_;
    std::shared_ptr<AuthMiddleware> auth_;
};

}  // namespace kd39::gateways::access
