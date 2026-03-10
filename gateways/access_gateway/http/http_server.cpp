#include "access_gateway/http/http_server.h"

#include <spdlog/spdlog.h>

#include "access_gateway/context/gateway_context.h"
#include "infrastructure/observability/metrics.h"

namespace kd39::gateways::access {

HttpServer::HttpServer(const std::string& address,
                       uint16_t port,
                       std::shared_ptr<GrpcRouter> router,
                       std::shared_ptr<AuthMiddleware> auth)
    : address_(address),
      port_(port),
      router_(std::move(router)),
      auth_(std::move(auth)) {}

HttpServer::~HttpServer() { Stop(); }

void HttpServer::Start() {
    spdlog::info("HttpServer prepared on {}:{} (testable request path enabled)", address_, port_);
}

void HttpServer::Stop() {
    spdlog::info("HttpServer stopping");
}

std::string HttpServer::HandleRequestForTesting(const std::string& path,
                                                const std::string& body,
                                                const std::string& auth_token,
                                                const std::unordered_map<std::string, std::string>& headers) const {
    if (path == "/metrics") {
        return kd39::infrastructure::observability::MetricsRegistry::Instance().RenderPrometheus();
    }

    auto auth_ctx = auth_ ? auth_->Authenticate(auth_token) : std::optional<common::RequestContext>{common::RequestContext{}};
    if (!auth_ctx.has_value()) {
        return R"({"error":"unauthorized"})";
    }

    auto ctx = BuildContextFromHeaders(
        headers.contains("x-request-id") ? headers.at("x-request-id") : "",
        headers.contains("x-trace-id") ? headers.at("x-trace-id") : "",
        headers.contains("x-traffic-tag") ? headers.at("x-traffic-tag") : "",
        headers.contains("x-client-version") ? headers.at("x-client-version") : "",
        headers.contains("x-zone") ? headers.at("x-zone") : "");
    ctx.user_id = auth_ctx->user_id;
    return router_ ? router_->Route(path, body, ctx) : R"({"error":"router_unavailable"})";
}

}  // namespace kd39::gateways::access
