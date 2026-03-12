#include "access_gateway/ws/ws_server.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <utility>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/cobalt/op.hpp>
#include <boost/cobalt/spawn.hpp>
#include <boost/cobalt/task.hpp>

#include "access_gateway/context/gateway_context.h"
#include "common/log/logger.h"

namespace kd39::gateways::access {
namespace {
namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace cobalt = boost::cobalt;
using tcp = net::ip::tcp;
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

std::string WsErrorJson(std::string error, std::string code, int status_code) {
    return nlohmann::json{
        {"error", std::move(error)},
        {"code", std::move(code)},
        {"status_code", status_code}}
        .dump();
}
}  // namespace

WsServer::WsServer(const std::string& address,
                   uint16_t port,
                   std::shared_ptr<GrpcRouter> router,
                   std::shared_ptr<AuthMiddleware> auth,
                   ServerRuntimeOptions runtime_options)
    : address_(address),
      port_(port),
      router_(std::move(router)),
      auth_(std::move(auth)),
      runtime_options_(std::move(runtime_options)) {}

WsServer::~WsServer() { Stop(); }

void WsServer::Start() {
    if (running_.exchange(true)) {
        return;
    }
    KD39_LOG_INFO("WsServer cobalt task path active");
    const int io_threads = std::max(1, runtime_options_.io_threads);
    KD39_LOG_INFO("WsServer runtime options: io_threads={}", io_threads);
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
        KD39_LOG_ERROR("WsServer failed to bind {}:{} err={}", address_, port_, ex.what());
        throw;
    }
    cobalt::spawn(*accept_io_context_, AcceptLoop(), net::detached);
    accept_thread_ = std::thread([this] { accept_io_context_->run(); });
    worker_threads_.clear();
    worker_threads_.reserve(static_cast<std::size_t>(io_threads));
    for (int i = 0; i < io_threads; ++i) {
        worker_threads_.emplace_back([this, i] { worker_io_contexts_[static_cast<std::size_t>(i)]->run(); });
    }
    KD39_LOG_INFO("WsServer listening on {}:{}", address_, port_);
}

void WsServer::Stop() {
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
    KD39_LOG_INFO("WsServer stopped");
}

std::string WsServer::HandleMessageForTesting(const std::string& payload,
                                              const std::string& auth_token) {
    const auto json = nlohmann::json::parse(payload.empty() ? "{}" : payload, nullptr, false);
    if (json.is_discarded()) {
        return WsErrorJson("invalid_json", "bad_request", 400);
    }
    const std::string effective_auth_token = !auth_token.empty() ? auth_token : json.value("auth_token", "");
    auto auth_ctx = auth_ ? auth_->Authenticate(effective_auth_token) : std::optional<common::RequestContext>{common::RequestContext{}};
    if (!auth_ctx.has_value()) {
        return WsErrorJson("unauthorized", "unauthenticated", 401);
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
    auto request_id = headers.contains("x-request-id") ? headers["x-request-id"] : generated_request_id;
    if (request_id.empty()) request_id = generated_request_id;
    auto trace_id = headers.contains("x-trace-id") ? headers["x-trace-id"] : request_id;
    if (trace_id.empty()) trace_id = request_id;
    auto traffic_tag = headers.contains("x-traffic-tag") ? headers["x-traffic-tag"] : "default";
    if (traffic_tag.empty()) traffic_tag = "default";
    auto client_version = headers.contains("x-client-version") ? headers["x-client-version"] : "unknown";
    if (client_version.empty()) client_version = "unknown";
    auto zone = headers.contains("x-zone") ? headers["x-zone"] : "default";
    if (zone.empty()) zone = "default";

    auto ctx = BuildContextFromHeaders(request_id, trace_id, traffic_tag, client_version, zone);
    ctx.user_id = auth_ctx->user_id;
    if (!router_) {
        return WsErrorJson("router_unavailable", "service_unavailable", 503);
    }
    auto routed = router_->RouteWithStatus(json.value("path", ""), json.value("body", "{}"), ctx);
    if (routed.body.empty()) {
        return WsErrorJson("empty_downstream_response", "bad_gateway", 502);
    }
    return routed.body;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
boost::cobalt::task<void> WsServer::AcceptLoop() {
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
                KD39_LOG_WARN("WsServer accept error: {}", ec.message());
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
            KD39_LOG_WARN("WsServer socket release error: {}", ec.message());
            continue;
        }
        tcp::socket worker_socket(worker_executor);
        try {
            worker_socket.assign(socket_protocol, native_handle);
        } catch (const std::exception& ex) {
            KD39_LOG_WARN("WsServer socket assign error: {}", ex.what());
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

boost::cobalt::task<void> WsServer::HandleSession(tcp::socket socket) {
    websocket::stream<tcp::socket> ws(std::move(socket));
    ws.read_message_max(kWsReadMessageMaxBytes);
    ws.write_buffer_bytes(kWsWriteBufferBytes);
    ws.auto_fragment(true);
    websocket::stream_base::timeout timeout_cfg{kWsHandshakeTimeout, kWsIdleTimeout, true};
    ws.set_option(timeout_cfg);

    boost::system::error_code ec;
    co_await ws.async_accept(net::redirect_error(cobalt::use_op, ec));
    if (ec) {
        if (ec != net::error::operation_aborted) {
            KD39_LOG_WARN("WsServer handshake error: {}", ec.message());
        }
        co_return;
    }
    const std::string auth_token;

    while (running_.load()) {
        beast::flat_buffer buffer;
        co_await ws.async_read(buffer, net::redirect_error(cobalt::use_op, ec));
        if (ec) {
            break;
        }
        const auto payload = beast::buffers_to_string(buffer.data());
        auto response = HandleMessageForTesting(payload, auth_token);
        if (response.size() > kWsResponseMaxBytes) {
            response = WsErrorJson("response_too_large", "payload_too_large", 413);
        }
        ws.text(true);
        co_await ws.async_write(net::buffer(response), net::redirect_error(cobalt::use_op, ec));
        if (ec) {
            break;
        }
    }
    if (ec && ec != websocket::error::closed && ec != net::error::operation_aborted) {
        KD39_LOG_WARN("WsServer session io error: {}", ec.message());
    }
    boost::system::error_code close_ec;
    co_await ws.async_close(websocket::close_code::normal, net::redirect_error(cobalt::use_op, close_ec));
    co_return;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

}  // namespace kd39::gateways::access
