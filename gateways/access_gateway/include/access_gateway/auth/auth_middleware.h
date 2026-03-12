#pragma once

#include <optional>
#include <string>

#include "common/context/request_context.h"

namespace kd39::gateways::access {

struct AuthOptions {
    std::string jwt_secret = "dev-secret";
    std::string jwt_issuer;
    std::string jwt_audience;
    bool allow_legacy_token = true;
};

class AuthMiddleware {
public:
    explicit AuthMiddleware(AuthOptions options = {});
    std::optional<common::RequestContext> Authenticate(const std::string& token) const;

private:
    AuthOptions options_;
};

}  // namespace kd39::gateways::access
