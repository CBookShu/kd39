#include "access_gateway/auth/auth_middleware.h"

#include "common/log/logger.h"

namespace kd39::gateways::access {

std::optional<common::RequestContext> AuthMiddleware::Authenticate(const std::string& token) const {
    if (token.empty()) {
        KD39_LOG_WARN("empty auth token");
        return std::nullopt;
    }

    auto normalized = token;
    if (normalized.rfind("Bearer ", 0) == 0) {
        normalized = normalized.substr(7);
    }

    common::RequestContext ctx;
    if (normalized == "dev-token") {
        ctx.user_id = "dev-user";
        return ctx;
    }

    if (normalized.rfind("user:", 0) == 0) {
        ctx.user_id = normalized.substr(5);
        return ctx;
    }

    KD39_LOG_WARN("unsupported auth token format");
    return std::nullopt;
}

}  // namespace kd39::gateways::access
