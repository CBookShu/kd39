#pragma once

#include <optional>
#include <string>

#include "common/context/request_context.h"

namespace kd39::gateways::access {

class AuthMiddleware {
public:
    std::optional<common::RequestContext> Authenticate(const std::string& token) const;
};

}  // namespace kd39::gateways::access
