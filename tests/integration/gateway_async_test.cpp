#if __has_include(<gtest/gtest.h>)

#include <atomic>
#include <chrono>
#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include "access_gateway/auth/auth_middleware.h"
#include "access_gateway/http/http_server.h"
#include "access_gateway/routing/grpc_router.h"
#include "access_gateway/ws/ws_server.h"
#include "infrastructure/storage/mysql/connection_pool.h"
#include "infrastructure/storage/redis/redis_client.h"
#include "user_service_impl.h"

namespace {
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

std::string HttpPost(const std::string& host,
                     const std::string& port,
                     const std::string& target,
                     const std::string& auth_token,
                     const std::string& body) {
    net::io_context ioc;
    tcp::resolver resolver(ioc);
    beast::tcp_stream stream(ioc);
    auto endpoints = resolver.resolve(host, port);
    stream.connect(endpoints);

    http::request<http::string_body> req{http::verb::post, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::authorization, auth_token);
    req.body() = body;
    req.prepare_payload();
    http::write(stream, req);

    beast::flat_buffer buffer;
    http::response<http::string_body> resp;
    http::read(stream, buffer, resp);
    std::string payload = resp.body();

    try {
        stream.socket().shutdown(tcp::socket::shutdown_both);
    } catch (...) {
    }
    return payload;
}
}  // namespace

TEST(GatewayAsyncIntegrationTest, HttpListenerHandlesConcurrentRequests) {
    auto mysql = kd39::infrastructure::storage::mysql::ConnectionPool::Create({"127.0.0.1", 3306, "root", "", "kd39", 2});
    auto redis = kd39::infrastructure::storage::redis::RedisClient::Create({"127.0.0.1", 6379, "", 0, 2});
    kd39::services::user::UserServiceImpl user_service({mysql, redis});

    int selected_port = 0;
    grpc::ServerBuilder builder;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &selected_port);
    builder.RegisterService(&user_service);
    auto grpc_server = builder.BuildAndStart();
    ASSERT_TRUE(grpc_server);

    auto router = std::make_shared<kd39::gateways::access::GrpcRouter>(
        kd39::gateways::access::RouterTargets{"127.0.0.1:50051", "127.0.0.1:" + std::to_string(selected_port), "127.0.0.1:50053"});
    auto auth = std::make_shared<kd39::gateways::access::AuthMiddleware>();
    kd39::gateways::access::HttpServer http_server("127.0.0.1", 0, router, auth);
    http_server.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto gateway_port = std::to_string(http_server.bound_port());

    constexpr int kWorkers = 8;
    std::atomic<int> success{0};
    std::vector<std::thread> workers;
    workers.reserve(kWorkers);
    for (int i = 0; i < kWorkers; ++i) {
        workers.emplace_back([&, i] {
            const auto response = HttpPost(
                "127.0.0.1", gateway_port, "/user/create", "Bearer user:load-user",
                nlohmann::json{{"nickname", "http-load-" + std::to_string(i)}}.dump());
            const auto json = nlohmann::json::parse(response, nullptr, false);
            if (!json.is_discarded() && json.contains("user_id")) {
                success.fetch_add(1);
            }
        });
    }
    for (auto& t : workers) {
        t.join();
    }

    EXPECT_EQ(success.load(), kWorkers);
    http_server.Stop();
    grpc_server->Shutdown();
}

TEST(GatewayAsyncIntegrationTest, WsListenerRoundTripAndStructuredErrors) {
    auto mysql = kd39::infrastructure::storage::mysql::ConnectionPool::Create({"127.0.0.1", 3306, "root", "", "kd39", 2});
    auto redis = kd39::infrastructure::storage::redis::RedisClient::Create({"127.0.0.1", 6379, "", 0, 2});
    kd39::services::user::UserServiceImpl user_service({mysql, redis});

    int selected_port = 0;
    grpc::ServerBuilder builder;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &selected_port);
    builder.RegisterService(&user_service);
    auto grpc_server = builder.BuildAndStart();
    ASSERT_TRUE(grpc_server);

    auto router = std::make_shared<kd39::gateways::access::GrpcRouter>(
        kd39::gateways::access::RouterTargets{"127.0.0.1:50051", "127.0.0.1:" + std::to_string(selected_port), "127.0.0.1:50053"});
    auto auth = std::make_shared<kd39::gateways::access::AuthMiddleware>();
    kd39::gateways::access::WsServer ws_server("127.0.0.1", 0, router, auth);
    ws_server.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto ws_port = std::to_string(ws_server.bound_port());

    net::io_context ioc;
    tcp::resolver resolver(ioc);
    websocket::stream<tcp::socket> ws(ioc);
    ws.set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
        req.set(http::field::authorization, "Bearer user:ws-user");
    }));
    auto endpoints = resolver.resolve("127.0.0.1", ws_port);
    net::connect(ws.next_layer(), endpoints);
    ws.handshake("127.0.0.1", "/");

    ws.write(net::buffer(std::string("{bad-json")));
    {
        beast::flat_buffer buffer;
        ws.read(buffer);
        const auto resp = beast::buffers_to_string(buffer.data());
        const auto json = nlohmann::json::parse(resp, nullptr, false);
        ASSERT_FALSE(json.is_discarded());
        EXPECT_EQ(json.value("code", ""), "bad_request");
    }

    const auto request = nlohmann::json{
        {"auth_token", "Bearer user:ws-user"},
        {"path", "/user/create"},
        {"body", R"({"nickname":"ws-load"})"},
        {"headers", nlohmann::json{{"x-request-id", "ws-1"}}}}
                             .dump();
    ws.write(net::buffer(request));
    {
        beast::flat_buffer buffer;
        ws.read(buffer);
        const auto resp = beast::buffers_to_string(buffer.data());
        const auto json = nlohmann::json::parse(resp, nullptr, false);
        ASSERT_FALSE(json.is_discarded());
        EXPECT_TRUE(json.contains("user_id"));
    }

    ws.close(websocket::close_code::normal);
    ws_server.Stop();
    grpc_server->Shutdown();
}

#endif  // __has_include(<gtest/gtest.h>)
