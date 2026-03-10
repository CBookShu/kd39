#include "access_gateway/ws/ws_server.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "access_gateway/context/gateway_context.h"

namespace kd39::gateways::access {

WsServer::WsServer(const std::string& address,
                   uint16_t port,
                   std::shared_ptr<GrpcRouter> router,
                   std::shared_ptr<AuthMiddleware> auth)
    : address_(address),
      port_(port),
      router_(std::move(router)),
      auth_(std::move(auth)) {}

WsServer::~WsServer() { Stop(); }

void WsServer::Start() {
    spdlog::info("WsServer prepared on {}:{} (testable message path enabled)", address_, port_);
}

void WsServer::Stop() {
    spdlog::info("WsServer stopping");
}

std::string WsServer::HandleMessageForTesting(const std::string& payload,
                                              const std::string& auth_token) const {
    auto auth_ctx = auth_ ? auth_->Authenticate(auth_token) : std::optional<common::RequestContext>{common::RequestContext{}};
    if (!auth_ctx.has_value()) {
        return R"({"error":"unauthorized"})";
    }
    const auto json = nlohmann::json::parse(payload.empty() ? "{}" : payload, nullptr, false);
    if (json.is_discarded()) {
        return R"({"error":"invalid_json"})";
    }
    auto ctx = BuildContextFromHeaders("", "", "", "", "");
    ctx.user_id = auth_ctx->user_id;
    return router_ ? router_->Route(json.value("path", ""), json.value("body", "{}"), ctx)
                   : R"({"error":"router_unavailable"})";
}

}  // namespace kd39::gateways::access
