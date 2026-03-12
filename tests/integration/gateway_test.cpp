#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <thread>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include "access_gateway/auth/auth_middleware.h"
#include "access_gateway/http/http_server.h"
#include "access_gateway/routing/grpc_router.h"
#include "user_service_impl.h"
#include "infrastructure/storage/mysql/connection_pool.h"
#include "infrastructure/storage/redis/redis_client.h"

namespace {
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class SlowUserService final : public kd39::api::user::UserService::Service {
public:
    grpc::Status GetUser(grpc::ServerContext*,
                         const kd39::api::user::GetUserRequest*,
                         kd39::api::user::GetUserResponse*) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        return grpc::Status::OK;
    }

    grpc::Status CreateUser(grpc::ServerContext*,
                            const kd39::api::user::CreateUserRequest*,
                            kd39::api::user::CreateUserResponse*) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        return grpc::Status::OK;
    }
};
}  // namespace

TEST(GatewayIntegrationTest, HttpGatewayRoutesToGrpcService) {
    auto mysql = kd39::infrastructure::storage::mysql::ConnectionPool::Create({"127.0.0.1", 3306, "root", "", "kd39", 2});
    auto redis = kd39::infrastructure::storage::redis::RedisClient::Create({"127.0.0.1", 6379, "", 0, 2});
    kd39::services::user::UserServiceImpl user_service({mysql, redis});

    int selected_port = 0;
    grpc::ServerBuilder builder;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &selected_port);
    builder.RegisterService(&user_service);
    auto server = builder.BuildAndStart();
    ASSERT_TRUE(server);

    auto router = std::make_shared<kd39::gateways::access::GrpcRouter>(
        kd39::gateways::access::RouterTargets{"127.0.0.1:50051", "127.0.0.1:" + std::to_string(selected_port), "127.0.0.1:50053"});
    auto auth = std::make_shared<kd39::gateways::access::AuthMiddleware>();
    kd39::gateways::access::HttpServer http("127.0.0.1", 18080, router, auth);

    const auto create_resp = http.HandleRequestForTesting(
        "/user/create",
        R"({"nickname":"gateway-user"})",
        "Bearer user:test-user",
        {{"x-request-id", "req-1"}, {"x-traffic-tag", "gray"}});

    const auto create_json = nlohmann::json::parse(create_resp);
    ASSERT_TRUE(create_json.contains("user_id"));

    const auto get_resp = http.HandleRequestForTesting(
        "/user/get",
        nlohmann::json{{"user_id", create_json["user_id"]}}.dump(),
        "Bearer user:test-user");

    const auto get_json = nlohmann::json::parse(get_resp);
    EXPECT_EQ(get_json["nickname"], "gateway-user");

    server->Shutdown();
}

TEST(GatewayIntegrationTest, GatewayRejectsInvalidAuthAndPayload) {
    auto router = std::make_shared<kd39::gateways::access::GrpcRouter>(
        kd39::gateways::access::RouterTargets{"127.0.0.1:50051", "127.0.0.1:50052", "127.0.0.1:50053"});
    auto auth = std::make_shared<kd39::gateways::access::AuthMiddleware>();
    kd39::gateways::access::HttpServer http("127.0.0.1", 18081, router, auth);

    const auto unauthorized = http.HandleRequestForTesting("/user/get", R"({})", "Bearer bad-token");
    EXPECT_NE(unauthorized.find("unauthorized"), std::string::npos);

    const auto invalid_json = http.HandleRequestForTesting("/user/get", "{not-json}", "Bearer user:test-user");
    EXPECT_NE(invalid_json.find("invalid_json"), std::string::npos);

    const auto route_not_found = http.HandleRequestForTesting("/unknown/path", "{}", "Bearer user:test-user");
    EXPECT_NE(route_not_found.find("route_not_found"), std::string::npos);
}

TEST(GatewayIntegrationTest, RouterReturnsDeadlineExceededOnSlowDownstream) {
    SlowUserService slow_user_service;
    int selected_port = 0;
    grpc::ServerBuilder builder;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &selected_port);
    builder.RegisterService(&slow_user_service);
    auto server = builder.BuildAndStart();
    ASSERT_TRUE(server);

    auto router = std::make_shared<kd39::gateways::access::GrpcRouter>(
        kd39::gateways::access::RouterTargets{"127.0.0.1:50051", "127.0.0.1:" + std::to_string(selected_port), "127.0.0.1:50053"},
        nullptr,
        kd39::gateways::access::RouterRuntimeConfig{10, 1, 0});
    kd39::common::RequestContext ctx;
    ctx.user_id = "test-user";
    auto result = router->RouteWithStatus("/user/get", R"({"user_id":"u1"})", ctx);
    EXPECT_EQ(result.status_code, 504);
    EXPECT_EQ(result.downstream_service, "user_service");
    EXPECT_NE(result.body.find("downstream_error"), std::string::npos);
    server->Shutdown();
}

TEST(GatewayIntegrationTest, WsUpgradeOnHttpListenerHandlesInvalidMessage) {
    auto router = std::make_shared<kd39::gateways::access::GrpcRouter>(
        kd39::gateways::access::RouterTargets{"127.0.0.1:50051", "127.0.0.1:50052", "127.0.0.1:50053"});
    auto auth = std::make_shared<kd39::gateways::access::AuthMiddleware>();
    kd39::gateways::access::HttpServer http_server("127.0.0.1", 0, router, auth);
    http_server.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto port = std::to_string(http_server.bound_port());

    net::io_context ioc;
    tcp::resolver resolver(ioc);
    websocket::stream<tcp::socket> ws(ioc);
    ws.set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
        req.set(http::field::authorization, "Bearer user:test-user");
    }));
    auto endpoints = resolver.resolve("127.0.0.1", port);
    net::connect(ws.next_layer(), endpoints);
    ws.handshake("127.0.0.1", "/");

    ws.write(net::buffer(std::string("{bad-json")));
    beast::flat_buffer buffer;
    ws.read(buffer);
    const auto invalid_json = beast::buffers_to_string(buffer.data());
    EXPECT_NE(invalid_json.find("bad_request"), std::string::npos);
    ws.close(websocket::close_code::normal);
    http_server.Stop();
}

TEST(GatewayIntegrationTest, RealHttpListenerRespondsThroughGateway) {
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

    net::io_context ioc;
    tcp::resolver resolver(ioc);
    beast::tcp_stream stream(ioc);
    auto endpoints = resolver.resolve("127.0.0.1", gateway_port);
    stream.connect(endpoints);

    http::request<http::string_body> req{http::verb::post, "/user/create", 11};
    req.set(http::field::host, "127.0.0.1");
    req.set(http::field::authorization, "Bearer user:test-user");
    req.body() = R"({"nickname":"real-http"})";
    req.prepare_payload();
    http::write(stream, req);

    beast::flat_buffer buffer;
    http::response<http::string_body> resp;
    http::read(stream, buffer, resp);
    EXPECT_EQ(resp.result(), http::status::ok);
    auto json = nlohmann::json::parse(resp.body(), nullptr, false);
    ASSERT_FALSE(json.is_discarded());
    EXPECT_TRUE(json.contains("user_id"));

    try {
        stream.socket().shutdown(tcp::socket::shutdown_both);
    } catch (...) {
    }
    http_server.Stop();
    grpc_server->Shutdown();
}
