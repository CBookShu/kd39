#include "access_gateway/http/http_server.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <utility>
#include <nlohmann/json.hpp>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/cobalt/op.hpp>
#include <boost/cobalt/spawn.hpp>
#include <boost/cobalt/task.hpp>

#include "access_gateway/context/gateway_context.h"
#include "common/log/logger.h"
#include "infrastructure/observability/metrics.h"

namespace kd39::gateways::access {
namespace {
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace cobalt = boost::cobalt;
using tcp = net::ip::tcp;
constexpr auto kHttpReadTimeout = std::chrono::seconds(15);
constexpr auto kHttpWriteTimeout = std::chrono::seconds(15);
constexpr auto kWsIdleTimeout = std::chrono::seconds(45);
constexpr auto kWsHandshakeTimeout = std::chrono::seconds(10);
constexpr std::size_t kWsReadMessageMaxBytes = 64 * 1024;
constexpr std::size_t kWsWriteBufferBytes = 16 * 1024;
constexpr std::size_t kWsResponseMaxBytes = 128 * 1024;

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string HeaderOrEmpty(const std::unordered_map<std::string, std::string>& headers, std::string key) {
    key = ToLowerAscii(std::move(key));
    if (auto it = headers.find(key); it != headers.end()) {
        return it->second;
    }
    return {};
}

std::unordered_map<std::string, std::string> NormalizeHeaders(const http::request<http::string_body>& req) {
    std::unordered_map<std::string, std::string> headers;
    for (const auto& field : req.base()) {
        headers[ToLowerAscii(std::string(field.name_string()))] = std::string(field.value());
    }
    return headers;
}

http::status ToHttpStatus(int status_code) {
    if (status_code >= 100 && status_code < 600) {
        return static_cast<http::status>(status_code);
    }
    return http::status::internal_server_error;
}

std::string WsErrorJson(std::string error, std::string code, int status_code) {
    return nlohmann::json{
        {"error", std::move(error)},
        {"code", std::move(code)},
        {"status_code", status_code}}
        .dump();
}
}  // namespace

HttpServer::HttpServer(const std::string& address,
                       uint16_t port,
                       std::shared_ptr<GrpcRouter> router,
                       std::shared_ptr<AuthMiddleware> auth,
                       ServerRuntimeOptions runtime_options)
    : address_(address),
      port_(port),
      router_(std::move(router)),
      auth_(std::move(auth)),
      runtime_options_(std::move(runtime_options)) {}

HttpServer::~HttpServer() { Stop(); }

void HttpServer::Start() {
    if (running_.exchange(true)) {
        return;
    }
    if (runtime_options_.cobalt_experimental) {
        KD39_LOG_INFO("HttpServer cobalt task path enabled by runtime flag");
    } else {
        KD39_LOG_INFO("HttpServer cobalt task path active (phase-1 baseline)");
    }
    const int io_threads = std::max(1, runtime_options_.io_threads);
    KD39_LOG_INFO("HttpServer runtime options: io_threads={} cobalt_experimental={}",
                  io_threads, runtime_options_.cobalt_experimental ? "on" : "off");

    accept_io_context_ = std::make_unique<net::io_context>(1);
    accept_work_guard_ = std::make_unique<WorkGuard>(net::make_work_guard(*accept_io_context_));
    worker_io_contexts_.clear();
    worker_io_contexts_.reserve(static_cast<std::size_t>(io_threads));
    worker_work_guards_.clear();
    worker_work_guards_.reserve(static_cast<std::size_t>(io_threads));
    for (int i = 0; i < io_threads; ++i) {
        worker_io_contexts_.emplace_back(std::make_unique<net::io_context>(1));
        worker_work_guards_.emplace_back(std::make_unique<WorkGuard>(net::make_work_guard(*worker_io_contexts_.back())));
    }
    next_worker_index_.store(0);
    try {
        auto endpoint = tcp::endpoint(net::ip::make_address(address_), port_);
        acceptor_ = std::make_unique<tcp::acceptor>(*accept_io_context_);
        acceptor_->open(endpoint.protocol());
        acceptor_->set_option(net::socket_base::reuse_address(true));
        acceptor_->bind(endpoint);
        acceptor_->listen(net::socket_base::max_listen_connections);
        port_ = acceptor_->local_endpoint().port();
    } catch (const std::exception& ex) {
        running_.store(false);
        KD39_LOG_ERROR("HttpServer failed to bind {}:{} err={}", address_, port_, ex.what());
        throw;
    }

    cobalt::spawn(*accept_io_context_, AcceptLoop(), net::detached);
    accept_thread_ = std::thread([this] { accept_io_context_->run(); });
    worker_threads_.clear();
    worker_threads_.reserve(static_cast<std::size_t>(io_threads));
    for (int i = 0; i < io_threads; ++i) {
        worker_threads_.emplace_back([this, i] { worker_io_contexts_[static_cast<std::size_t>(i)]->run(); });
    }
    KD39_LOG_INFO("HttpServer listening on {}:{}", address_, port_);
}

void HttpServer::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (acceptor_) {
        try {
            acceptor_->close();
        } catch (...) {
        }
    }
    accept_work_guard_.reset();
    worker_work_guards_.clear();
    if (accept_io_context_) {
        accept_io_context_->stop();
    }
    for (auto& worker_io_context : worker_io_contexts_) {
        if (worker_io_context) {
            worker_io_context->stop();
        }
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    worker_threads_.clear();
    worker_io_contexts_.clear();
    accept_work_guard_.reset();
    acceptor_.reset();
    accept_io_context_.reset();
    KD39_LOG_INFO("HttpServer stopped");
}

std::string HttpServer::HandleRequestForTesting(const std::string& path,
                                                const std::string& body,
                                                const std::string& auth_token,
                                                const std::unordered_map<std::string, std::string>& headers,
                                                const std::string& method) {
    auto normalized_headers = headers;
    std::unordered_map<std::string, std::string> lowered;
    for (const auto& [key, value] : normalized_headers) {
        lowered[ToLowerAscii(key)] = value;
    }
    return HandleRequest(method, path, body, auth_token, lowered).body;
}

HttpServer::HttpResponse HttpServer::HandleRequest(
    const std::string& method,
    const std::string& path,
    const std::string& body,
    const std::string& auth_token,
    const std::unordered_map<std::string, std::string>& headers) {
    if (path == "/health") {
        return {200, R"({"status":"ok","service":"access_gateway"})", "application/json"};
    }
    if (path == "/ready") {
        if (router_) {
            return {200, R"({"status":"ready"})", "application/json"};
        }
        return {503, R"({"status":"not_ready","reason":"router_unavailable"})", "application/json"};
    }
    if (path == "/metrics") {
        return {200, kd39::infrastructure::observability::MetricsRegistry::Instance().RenderPrometheus(), "text/plain; charset=utf-8"};
    }

    const auto normalized_method = ToLowerAscii(method);
    if (path == "/admin/log-level") {
        auto admin_ctx = auth_ ? auth_->Authenticate(auth_token) : std::optional<common::RequestContext>{common::RequestContext{}};
        if (!admin_ctx.has_value() || admin_ctx->user_id != "dev-user") {
            return {401, R"({"error":"unauthorized_admin"})", "application/json"};
        }
        if (normalized_method == "get") {
            const auto level = kd39::common::log::LogLevelName(kd39::common::log::GetLogLevel());
            return {200, nlohmann::json{{"log_level", std::string(level)}}.dump(), "application/json"};
        }
        if (normalized_method == "post") {
            const auto json = nlohmann::json::parse(body.empty() ? "{}" : body, nullptr, false);
            if (json.is_discarded() || !json.contains("log_level") || !json["log_level"].is_string()) {
                return {400, R"({"error":"invalid_json"})", "application/json"};
            }
            if (!kd39::common::log::SetLogLevel(json["log_level"].get<std::string>())) {
                return {400, R"({"error":"invalid_log_level"})", "application/json"};
            }
            return {200,
                    nlohmann::json{{"log_level", std::string(kd39::common::log::LogLevelName(kd39::common::log::GetLogLevel()))}}.dump(),
                    "application/json"};
        }
        return {405, R"({"error":"method_not_allowed"})", "application/json"};
    }

    if (path == "/admin/runtime-config") {
        auto admin_ctx = auth_ ? auth_->Authenticate(auth_token) : std::optional<common::RequestContext>{common::RequestContext{}};
        if (!admin_ctx.has_value() || admin_ctx->user_id != "dev-user") {
            return {401, R"({"error":"unauthorized_admin"})", "application/json"};
        }
        if (!router_) {
            return {503, R"({"error":"router_unavailable"})", "application/json"};
        }
        if (normalized_method == "get") {
            const auto cfg = router_->GetRuntimeConfig();
            return {200,
                    nlohmann::json{
                        {"grpc_timeout_ms", cfg.grpc_timeout_ms},
                        {"retry_attempts", cfg.retry_attempts},
                        {"retry_backoff_ms", cfg.retry_backoff_ms}}
                        .dump(),
                    "application/json"};
        }
        if (normalized_method == "post") {
            const auto json = nlohmann::json::parse(body.empty() ? "{}" : body, nullptr, false);
            if (json.is_discarded()) {
                return {400, R"({"error":"invalid_json"})", "application/json"};
            }
            RouterRuntimeConfig cfg = router_->GetRuntimeConfig();
            if (json.contains("grpc_timeout_ms")) cfg.grpc_timeout_ms = json["grpc_timeout_ms"].get<int>();
            if (json.contains("retry_attempts")) cfg.retry_attempts = json["retry_attempts"].get<int>();
            if (json.contains("retry_backoff_ms")) cfg.retry_backoff_ms = json["retry_backoff_ms"].get<int>();
            router_->UpdateRuntimeConfig(cfg);
            const auto effective = router_->GetRuntimeConfig();
            return {200,
                    nlohmann::json{
                        {"grpc_timeout_ms", effective.grpc_timeout_ms},
                        {"retry_attempts", effective.retry_attempts},
                        {"retry_backoff_ms", effective.retry_backoff_ms}}
                        .dump(),
                    "application/json"};
        }
        return {405, R"({"error":"method_not_allowed"})", "application/json"};
    }

    auto auth_ctx = auth_ ? auth_->Authenticate(auth_token) : std::optional<common::RequestContext>{common::RequestContext{}};
    if (!auth_ctx.has_value()) {
        return {401, R"({"error":"unauthorized"})", "application/json"};
    }
    if (!router_) {
        return {503, R"({"error":"router_unavailable"})", "application/json"};
    }

    const auto generated_request_id = "gw-req-" + std::to_string(request_seq_.fetch_add(1));
    auto request_id = HeaderOrEmpty(headers, "x-request-id");
    if (request_id.empty()) request_id = generated_request_id;
    auto trace_id = HeaderOrEmpty(headers, "x-trace-id");
    if (trace_id.empty()) trace_id = request_id;
    auto traffic_tag = HeaderOrEmpty(headers, "x-traffic-tag");
    if (traffic_tag.empty()) traffic_tag = "default";
    auto client_version = HeaderOrEmpty(headers, "x-client-version");
    if (client_version.empty()) client_version = "unknown";
    auto zone = HeaderOrEmpty(headers, "x-zone");
    if (zone.empty()) zone = "default";

    auto ctx = BuildContextFromHeaders(request_id, trace_id, traffic_tag, client_version, zone);
    ctx.user_id = auth_ctx->user_id;
    auto routed = router_->RouteWithStatus(path, body, ctx);
    return {routed.status_code, routed.body, "application/json"};
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
boost::cobalt::task<HttpServer::HttpResponse> HttpServer::HandleRequestAsync(
    const std::string& method,
    const std::string& path,
    const std::string& body,
    const std::string& auth_token,
    const std::unordered_map<std::string, std::string>& headers) {
    if (path == "/health") {
        co_return HttpResponse{200, R"({"status":"ok","service":"access_gateway"})", "application/json"};
    }
    if (path == "/ready") {
        if (router_) {
            co_return HttpResponse{200, R"({"status":"ready"})", "application/json"};
        }
        co_return HttpResponse{503, R"({"status":"not_ready","reason":"router_unavailable"})", "application/json"};
    }
    if (path == "/metrics") {
        co_return HttpResponse{
            200, kd39::infrastructure::observability::MetricsRegistry::Instance().RenderPrometheus(), "text/plain; charset=utf-8"};
    }

    const auto normalized_method = ToLowerAscii(method);
    if (path == "/admin/log-level") {
        auto admin_ctx = auth_ ? auth_->Authenticate(auth_token) : std::optional<common::RequestContext>{common::RequestContext{}};
        if (!admin_ctx.has_value() || admin_ctx->user_id != "dev-user") {
            co_return HttpResponse{401, R"({"error":"unauthorized_admin"})", "application/json"};
        }
        if (normalized_method == "get") {
            const auto level = kd39::common::log::LogLevelName(kd39::common::log::GetLogLevel());
            co_return HttpResponse{200, nlohmann::json{{"log_level", std::string(level)}}.dump(), "application/json"};
        }
        if (normalized_method == "post") {
            const auto json = nlohmann::json::parse(body.empty() ? "{}" : body, nullptr, false);
            if (json.is_discarded() || !json.contains("log_level") || !json["log_level"].is_string()) {
                co_return HttpResponse{400, R"({"error":"invalid_json"})", "application/json"};
            }
            if (!kd39::common::log::SetLogLevel(json["log_level"].get<std::string>())) {
                co_return HttpResponse{400, R"({"error":"invalid_log_level"})", "application/json"};
            }
            co_return HttpResponse{
                200,
                nlohmann::json{{"log_level", std::string(kd39::common::log::LogLevelName(kd39::common::log::GetLogLevel()))}}.dump(),
                "application/json"};
        }
        co_return HttpResponse{405, R"({"error":"method_not_allowed"})", "application/json"};
    }

    if (path == "/admin/runtime-config") {
        auto admin_ctx = auth_ ? auth_->Authenticate(auth_token) : std::optional<common::RequestContext>{common::RequestContext{}};
        if (!admin_ctx.has_value() || admin_ctx->user_id != "dev-user") {
            co_return HttpResponse{401, R"({"error":"unauthorized_admin"})", "application/json"};
        }
        if (!router_) {
            co_return HttpResponse{503, R"({"error":"router_unavailable"})", "application/json"};
        }
        if (normalized_method == "get") {
            const auto cfg = router_->GetRuntimeConfig();
            co_return HttpResponse{
                200,
                nlohmann::json{
                    {"grpc_timeout_ms", cfg.grpc_timeout_ms},
                    {"retry_attempts", cfg.retry_attempts},
                    {"retry_backoff_ms", cfg.retry_backoff_ms}}
                    .dump(),
                "application/json"};
        }
        if (normalized_method == "post") {
            const auto json = nlohmann::json::parse(body.empty() ? "{}" : body, nullptr, false);
            if (json.is_discarded()) {
                co_return HttpResponse{400, R"({"error":"invalid_json"})", "application/json"};
            }
            RouterRuntimeConfig cfg = router_->GetRuntimeConfig();
            if (json.contains("grpc_timeout_ms")) cfg.grpc_timeout_ms = json["grpc_timeout_ms"].get<int>();
            if (json.contains("retry_attempts")) cfg.retry_attempts = json["retry_attempts"].get<int>();
            if (json.contains("retry_backoff_ms")) cfg.retry_backoff_ms = json["retry_backoff_ms"].get<int>();
            router_->UpdateRuntimeConfig(cfg);
            const auto effective = router_->GetRuntimeConfig();
            co_return HttpResponse{
                200,
                nlohmann::json{
                    {"grpc_timeout_ms", effective.grpc_timeout_ms},
                    {"retry_attempts", effective.retry_attempts},
                    {"retry_backoff_ms", effective.retry_backoff_ms}}
                    .dump(),
                "application/json"};
        }
        co_return HttpResponse{405, R"({"error":"method_not_allowed"})", "application/json"};
    }

    auto auth_ctx = auth_ ? auth_->Authenticate(auth_token) : std::optional<common::RequestContext>{common::RequestContext{}};
    if (!auth_ctx.has_value()) {
        co_return HttpResponse{401, R"({"error":"unauthorized"})", "application/json"};
    }
    if (!router_) {
        co_return HttpResponse{503, R"({"error":"router_unavailable"})", "application/json"};
    }

    const auto generated_request_id = "gw-req-" + std::to_string(request_seq_.fetch_add(1));
    auto request_id = HeaderOrEmpty(headers, "x-request-id");
    if (request_id.empty()) request_id = generated_request_id;
    auto trace_id = HeaderOrEmpty(headers, "x-trace-id");
    if (trace_id.empty()) trace_id = request_id;
    auto traffic_tag = HeaderOrEmpty(headers, "x-traffic-tag");
    if (traffic_tag.empty()) traffic_tag = "default";
    auto client_version = HeaderOrEmpty(headers, "x-client-version");
    if (client_version.empty()) client_version = "unknown";
    auto zone = HeaderOrEmpty(headers, "x-zone");
    if (zone.empty()) zone = "default";

    auto ctx = BuildContextFromHeaders(request_id, trace_id, traffic_tag, client_version, zone);
    ctx.user_id = auth_ctx->user_id;
    auto routed = co_await router_->RouteWithStatusAsync(path, body, ctx);
    co_return HttpResponse{routed.status_code, routed.body, "application/json"};
}

std::string HttpServer::HandleWsMessage(const std::string& payload,
                                        const std::string& authenticated_user_id) {
    const auto json = nlohmann::json::parse(payload.empty() ? "{}" : payload, nullptr, false);
    if (json.is_discarded()) {
        return WsErrorJson("invalid_json", "bad_request", 400);
    }

    std::unordered_map<std::string, std::string> headers;
    if (auto it = json.find("headers"); it != json.end() && it->is_object()) {
        for (const auto& [key, value] : it->items()) {
            if (value.is_string()) {
                headers[ToLowerAscii(key)] = value.get<std::string>();
            }
        }
    }

    const auto generated_request_id = "gw-ws-" + std::to_string(request_seq_.fetch_add(1));
    auto request_id = HeaderOrEmpty(headers, "x-request-id");
    if (request_id.empty()) request_id = generated_request_id;
    auto trace_id = HeaderOrEmpty(headers, "x-trace-id");
    if (trace_id.empty()) trace_id = request_id;
    auto traffic_tag = HeaderOrEmpty(headers, "x-traffic-tag");
    if (traffic_tag.empty()) traffic_tag = "default";
    auto client_version = HeaderOrEmpty(headers, "x-client-version");
    if (client_version.empty()) client_version = "unknown";
    auto zone = HeaderOrEmpty(headers, "x-zone");
    if (zone.empty()) zone = "default";

    auto ctx = BuildContextFromHeaders(request_id, trace_id, traffic_tag, client_version, zone);
    ctx.user_id = authenticated_user_id;
    if (!router_) {
        return WsErrorJson("router_unavailable", "service_unavailable", 503);
    }
    auto routed = router_->RouteWithStatus(json.value("path", ""), json.value("body", "{}"), ctx);
    if (routed.body.empty()) {
        return WsErrorJson("empty_downstream_response", "bad_gateway", 502);
    }
    return routed.body;
}

boost::cobalt::task<std::string> HttpServer::HandleWsMessageAsync(const std::string& payload,
                                                                  const std::string& authenticated_user_id) {
    const auto json = nlohmann::json::parse(payload.empty() ? "{}" : payload, nullptr, false);
    if (json.is_discarded()) {
        co_return WsErrorJson("invalid_json", "bad_request", 400);
    }

    std::unordered_map<std::string, std::string> headers;
    if (auto it = json.find("headers"); it != json.end() && it->is_object()) {
        for (const auto& [key, value] : it->items()) {
            if (value.is_string()) {
                headers[ToLowerAscii(key)] = value.get<std::string>();
            }
        }
    }

    const auto generated_request_id = "gw-ws-" + std::to_string(request_seq_.fetch_add(1));
    auto request_id = HeaderOrEmpty(headers, "x-request-id");
    if (request_id.empty()) request_id = generated_request_id;
    auto trace_id = HeaderOrEmpty(headers, "x-trace-id");
    if (trace_id.empty()) trace_id = request_id;
    auto traffic_tag = HeaderOrEmpty(headers, "x-traffic-tag");
    if (traffic_tag.empty()) traffic_tag = "default";
    auto client_version = HeaderOrEmpty(headers, "x-client-version");
    if (client_version.empty()) client_version = "unknown";
    auto zone = HeaderOrEmpty(headers, "x-zone");
    if (zone.empty()) zone = "default";

    auto ctx = BuildContextFromHeaders(request_id, trace_id, traffic_tag, client_version, zone);
    ctx.user_id = authenticated_user_id;
    if (!router_) {
        co_return WsErrorJson("router_unavailable", "service_unavailable", 503);
    }
    auto routed = co_await router_->RouteWithStatusAsync(json.value("path", ""), json.value("body", "{}"), ctx);
    if (routed.body.empty()) {
        co_return WsErrorJson("empty_downstream_response", "bad_gateway", 502);
    }
    co_return routed.body;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
boost::cobalt::task<void> HttpServer::AcceptLoop() {
    const auto accept_executor = acceptor_->get_executor();
    boost::system::error_code local_ep_ec;
    const auto local_ep = acceptor_->local_endpoint(local_ep_ec);
    const auto socket_protocol = local_ep_ec ? tcp::v4() : local_ep.protocol();
    while (running_.load()) {
        boost::system::error_code ec;
        tcp::socket accepted_socket(accept_executor);
        co_await acceptor_->async_accept(accepted_socket, net::redirect_error(cobalt::use_op, ec));
        if (ec) {
            if (running_.load() && ec != net::error::operation_aborted) {
                KD39_LOG_WARN("HttpServer accept error: {}", ec.message());
            }
            continue;
        }

        if (worker_io_contexts_.empty()) {
            try {
                accepted_socket.close();
            } catch (...) {
            }
            continue;
        }

        const auto worker_index =
            next_worker_index_.fetch_add(1, std::memory_order_relaxed) % worker_io_contexts_.size();
        const auto worker_executor = worker_io_contexts_[worker_index]->get_executor();

        const auto native_handle = accepted_socket.release(ec);
        if (ec) {
            KD39_LOG_WARN("HttpServer socket release error: {}", ec.message());
            continue;
        }
        tcp::socket worker_socket(worker_executor);
        try {
            worker_socket.assign(socket_protocol, native_handle);
        } catch (const std::exception& ex) {
            KD39_LOG_WARN("HttpServer socket assign error: {}", ex.what());
            tcp::socket cleanup_socket(accept_executor);
            try {
                cleanup_socket.assign(socket_protocol, native_handle);
                cleanup_socket.close();
            } catch (...) {
            }
            continue;
        }
        cobalt::spawn(worker_executor, HandleSession(std::move(worker_socket)), net::detached);
    }
    co_return;
}

boost::cobalt::task<void> HttpServer::HandleSession(tcp::socket socket) {
    beast::flat_buffer buffer;
    boost::system::error_code ec;
    http::request<http::string_body> req;
    beast::tcp_stream stream(std::move(socket));
    stream.expires_after(kHttpReadTimeout);
    co_await http::async_read(stream, buffer, req, net::redirect_error(cobalt::use_op, ec));
    if (ec) {
        if (ec != net::error::operation_aborted) {
            KD39_LOG_WARN("HttpServer read error: {}", ec.message());
        }
        co_return;
    }

    if (websocket::is_upgrade(req)) {
        auto auth_ctx = auth_ ? auth_->Authenticate(std::string(req[http::field::authorization]))
                              : std::optional<common::RequestContext>{common::RequestContext{}};
        if (!auth_ctx.has_value()) {
            http::response<http::string_body> response;
            response.result(http::status::unauthorized);
            response.version(req.version());
            response.set(http::field::server, "kd39-access-gateway");
            response.set(http::field::content_type, "application/json");
            response.body() = R"({"error":"unauthorized"})";
            response.keep_alive(false);
            response.prepare_payload();
            stream.expires_after(kHttpWriteTimeout);
            co_await http::async_write(stream, response, net::redirect_error(cobalt::use_op, ec));
            if (ec && ec != net::error::operation_aborted) {
                KD39_LOG_WARN("HttpServer ws unauthorized response write error: {}", ec.message());
            }
            try {
                stream.socket().shutdown(tcp::socket::shutdown_both);
            } catch (...) {
            }
            co_return;
        }

        websocket::stream<beast::tcp_stream> ws(std::move(stream));
        ws.read_message_max(kWsReadMessageMaxBytes);
        ws.write_buffer_bytes(kWsWriteBufferBytes);
        ws.auto_fragment(true);
        websocket::stream_base::timeout timeout_cfg{kWsHandshakeTimeout, kWsIdleTimeout, true};
        ws.set_option(timeout_cfg);

        co_await ws.async_accept(req, net::redirect_error(cobalt::use_op, ec));
        if (ec) {
            if (ec != net::error::operation_aborted) {
                KD39_LOG_WARN("HttpServer ws handshake error: {}", ec.message());
            }
            co_return;
        }

        while (running_.load()) {
            beast::flat_buffer ws_buffer;
            co_await ws.async_read(ws_buffer, net::redirect_error(cobalt::use_op, ec));
            if (ec) {
                break;
            }

            const auto payload = beast::buffers_to_string(ws_buffer.data());
            auto response_payload = co_await HandleWsMessageAsync(payload, auth_ctx->user_id);
            if (response_payload.size() > kWsResponseMaxBytes) {
                response_payload = WsErrorJson("response_too_large", "payload_too_large", 413);
            }
            ws.text(true);
            co_await ws.async_write(net::buffer(response_payload), net::redirect_error(cobalt::use_op, ec));
            if (ec) {
                break;
            }
        }
        if (ec && ec != websocket::error::closed && ec != net::error::operation_aborted) {
            KD39_LOG_WARN("HttpServer ws session io error: {}", ec.message());
        }
        boost::system::error_code close_ec;
        co_await ws.async_close(websocket::close_code::normal, net::redirect_error(cobalt::use_op, close_ec));
        co_return;
    }

    const auto headers = NormalizeHeaders(req);
    const auto response_payload = co_await HandleRequestAsync(std::string(req.method_string()),
                                                              std::string(req.target()),
                                                              req.body(),
                                                              std::string(req[http::field::authorization]),
                                                              headers);

    http::response<http::string_body> response;
    response.result(ToHttpStatus(response_payload.status_code));
    response.version(req.version());
    response.set(http::field::server, "kd39-access-gateway");
    response.set(http::field::content_type, response_payload.content_type);
    response.body() = response_payload.body;
    response.keep_alive(false);
    response.prepare_payload();
    stream.expires_after(kHttpWriteTimeout);
    co_await http::async_write(stream, response, net::redirect_error(cobalt::use_op, ec));
    if (ec && ec != net::error::operation_aborted) {
        KD39_LOG_WARN("HttpServer write error: {}", ec.message());
    }
    try {
        stream.socket().shutdown(tcp::socket::shutdown_both);
    } catch (...) {
    }
    co_return;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

}  // namespace kd39::gateways::access
