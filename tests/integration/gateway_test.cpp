#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <atomic>
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
#include "common/log/logger.h"
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

class CancelAwareUserService final : public kd39::api::user::UserService::Service {
public:
    grpc::Status GetUser(grpc::ServerContext* context,
                         const kd39::api::user::GetUserRequest*,
                         kd39::api::user::GetUserResponse*) override {
        return WaitForCancellation(context);
    }

    grpc::Status CreateUser(grpc::ServerContext* context,
                            const kd39::api::user::CreateUserRequest*,
                            kd39::api::user::CreateUserResponse*) override {
        return WaitForCancellation(context);
    }

    bool cancellation_observed() const { return cancellation_observed_.load(std::memory_order_acquire); }
    int request_count() const { return request_count_.load(std::memory_order_acquire); }

private:
    grpc::Status WaitForCancellation(grpc::ServerContext* context) {
        request_count_.fetch_add(1, std::memory_order_acq_rel);
        for (int i = 0; i < 300; ++i) {
            if (context->IsCancelled()) {
                cancellation_observed_.store(true, std::memory_order_release);
                return grpc::Status(grpc::StatusCode::CANCELLED, "cancelled_by_deadline");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return grpc::Status::OK;
    }

    std::atomic<bool> cancellation_observed_{false};
    std::atomic<int> request_count_{0};
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

TEST(GatewayIntegrationTest, OpsAndAdminEndpointsWorkAsExpected) {
    auto router = std::make_shared<kd39::gateways::access::GrpcRouter>(
        kd39::gateways::access::RouterTargets{"127.0.0.1:50051", "127.0.0.1:50052", "127.0.0.1:50053"},
        nullptr,
        kd39::gateways::access::RouterRuntimeConfig{1500, 2, 5});
    auto auth = std::make_shared<kd39::gateways::access::AuthMiddleware>();
    kd39::gateways::access::HttpServer http("127.0.0.1", 18082, router, auth);

    const auto health = nlohmann::json::parse(http.HandleRequestForTesting("/health", "", ""), nullptr, false);
    ASSERT_FALSE(health.is_discarded());
    EXPECT_EQ(health.value("status", ""), "ok");
    EXPECT_EQ(health.value("service", ""), "access_gateway");

    const auto ready = nlohmann::json::parse(http.HandleRequestForTesting("/ready", "", ""), nullptr, false);
    ASSERT_FALSE(ready.is_discarded());
    EXPECT_EQ(ready.value("status", ""), "ready");

    const auto unauthorized_admin = nlohmann::json::parse(
        http.HandleRequestForTesting("/admin/runtime-config", "{}", "Bearer user:test-user", {}, "GET"), nullptr, false);
    ASSERT_FALSE(unauthorized_admin.is_discarded());
    EXPECT_EQ(unauthorized_admin.value("error", ""), "unauthorized_admin");

    const auto runtime_get = nlohmann::json::parse(
        http.HandleRequestForTesting("/admin/runtime-config", "{}", "Bearer user:dev-user", {}, "GET"), nullptr, false);
    ASSERT_FALSE(runtime_get.is_discarded());
    EXPECT_EQ(runtime_get.value("grpc_timeout_ms", 0), 1500);
    EXPECT_EQ(runtime_get.value("retry_attempts", 0), 2);
    EXPECT_EQ(runtime_get.value("retry_backoff_ms", 0), 5);

    const auto runtime_post = nlohmann::json::parse(
        http.HandleRequestForTesting(
            "/admin/runtime-config",
            R"({"grpc_timeout_ms":2222,"retry_attempts":3,"retry_backoff_ms":11})",
            "Bearer user:dev-user",
            {},
            "POST"),
        nullptr,
        false);
    ASSERT_FALSE(runtime_post.is_discarded());
    EXPECT_EQ(runtime_post.value("grpc_timeout_ms", 0), 2222);
    EXPECT_EQ(runtime_post.value("retry_attempts", 0), 3);
    EXPECT_EQ(runtime_post.value("retry_backoff_ms", 0), 11);

    const auto runtime_method = nlohmann::json::parse(
        http.HandleRequestForTesting("/admin/runtime-config", "{}", "Bearer user:dev-user", {}, "DELETE"), nullptr, false);
    ASSERT_FALSE(runtime_method.is_discarded());
    EXPECT_EQ(runtime_method.value("error", ""), "method_not_allowed");

    static_cast<void>(http.HandleRequestForTesting("/unknown/path", "{}", "Bearer user:test-user"));
    const auto metrics_payload = http.HandleRequestForTesting("/metrics", "", "");
    EXPECT_NE(metrics_payload.find("gateway_requests_total"), std::string::npos);

    kd39::gateways::access::HttpServer no_router("127.0.0.1", 18083, nullptr, auth);
    const auto not_ready = nlohmann::json::parse(no_router.HandleRequestForTesting("/ready", "", ""), nullptr, false);
    ASSERT_FALSE(not_ready.is_discarded());
    EXPECT_EQ(not_ready.value("status", ""), "not_ready");
}

TEST(GatewayIntegrationTest, AdminLogLevelEndpointSupportsGetAndPost) {
    auto router = std::make_shared<kd39::gateways::access::GrpcRouter>(
        kd39::gateways::access::RouterTargets{"127.0.0.1:50051", "127.0.0.1:50052", "127.0.0.1:50053"});
    auto auth = std::make_shared<kd39::gateways::access::AuthMiddleware>();
    kd39::gateways::access::HttpServer http("127.0.0.1", 18084, router, auth);

    const auto original_level = kd39::common::log::GetLogLevel();
    const std::string original_level_name(kd39::common::log::LogLevelName(original_level));

    const auto unauthorized_admin = nlohmann::json::parse(
        http.HandleRequestForTesting("/admin/log-level", "{}", "Bearer user:test-user", {}, "GET"), nullptr, false);
    ASSERT_FALSE(unauthorized_admin.is_discarded());
    EXPECT_EQ(unauthorized_admin.value("error", ""), "unauthorized_admin");

    const auto level_get = nlohmann::json::parse(
        http.HandleRequestForTesting("/admin/log-level", "{}", "Bearer user:dev-user", {}, "GET"), nullptr, false);
    ASSERT_FALSE(level_get.is_discarded());
    EXPECT_TRUE(level_get.contains("log_level"));

    const auto invalid_json = nlohmann::json::parse(
        http.HandleRequestForTesting("/admin/log-level", "{bad-json}", "Bearer user:dev-user", {}, "POST"), nullptr, false);
    ASSERT_FALSE(invalid_json.is_discarded());
    EXPECT_EQ(invalid_json.value("error", ""), "invalid_json");

    const auto invalid_level = nlohmann::json::parse(
        http.HandleRequestForTesting("/admin/log-level", R"({"log_level":"not-a-level"})", "Bearer user:dev-user", {}, "POST"),
        nullptr,
        false);
    ASSERT_FALSE(invalid_level.is_discarded());
    EXPECT_EQ(invalid_level.value("error", ""), "invalid_log_level");

    const auto level_post = nlohmann::json::parse(
        http.HandleRequestForTesting("/admin/log-level", R"({"log_level":"warn"})", "Bearer user:dev-user", {}, "POST"), nullptr, false);
    ASSERT_FALSE(level_post.is_discarded());
    EXPECT_EQ(level_post.value("log_level", ""), "warn");

    EXPECT_TRUE(kd39::common::log::SetLogLevel(original_level_name));

    const auto level_method = nlohmann::json::parse(
        http.HandleRequestForTesting("/admin/log-level", "{}", "Bearer user:dev-user", {}, "DELETE"), nullptr, false);
    ASSERT_FALSE(level_method.is_discarded());
    EXPECT_EQ(level_method.value("error", ""), "method_not_allowed");
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

TEST(GatewayIntegrationTest, RouterRetryBudgetAffectsTailLatency) {
    SlowUserService slow_user_service;
    int selected_port = 0;
    grpc::ServerBuilder builder;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &selected_port);
    builder.RegisterService(&slow_user_service);
    auto server = builder.BuildAndStart();
    ASSERT_TRUE(server);

    const auto target = "127.0.0.1:" + std::to_string(selected_port);
    kd39::common::RequestContext ctx;
    ctx.user_id = "test-user";

    const auto measure_elapsed_ms = [&](kd39::gateways::access::RouterRuntimeConfig runtime_cfg) {
        auto router = std::make_shared<kd39::gateways::access::GrpcRouter>(
            kd39::gateways::access::RouterTargets{"127.0.0.1:50051", target, "127.0.0.1:50053"},
            nullptr,
            runtime_cfg);
        const auto start = std::chrono::steady_clock::now();
        auto result = router->RouteWithStatus("/user/get", R"({"user_id":"u1"})", ctx);
        const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        EXPECT_EQ(result.status_code, 504);
        EXPECT_EQ(result.downstream_service, "user_service");
        return elapsed_ms;
    };

    const auto single_attempt_ms = measure_elapsed_ms(kd39::gateways::access::RouterRuntimeConfig{100, 1, 0});
    const auto multi_attempt_ms = measure_elapsed_ms(kd39::gateways::access::RouterRuntimeConfig{100, 3, 20});

    EXPECT_GE(single_attempt_ms, 80);
    EXPECT_LT(single_attempt_ms, 400);
    EXPECT_GE(multi_attempt_ms, single_attempt_ms + 120);
    EXPECT_LT(multi_attempt_ms, 1200);

    server->Shutdown();
}

TEST(GatewayIntegrationTest, RouterDeadlinePropagatesCancellationToDownstream) {
    CancelAwareUserService cancel_aware_service;
    int selected_port = 0;
    grpc::ServerBuilder builder;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &selected_port);
    builder.RegisterService(&cancel_aware_service);
    auto server = builder.BuildAndStart();
    ASSERT_TRUE(server);

    auto router = std::make_shared<kd39::gateways::access::GrpcRouter>(
        kd39::gateways::access::RouterTargets{"127.0.0.1:50051", "127.0.0.1:" + std::to_string(selected_port), "127.0.0.1:50053"},
        nullptr,
        kd39::gateways::access::RouterRuntimeConfig{100, 1, 0});
    kd39::common::RequestContext ctx;
    ctx.user_id = "test-user";

    const auto start = std::chrono::steady_clock::now();
    auto result = router->RouteWithStatus("/user/get", R"({"user_id":"u1"})", ctx);
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    EXPECT_EQ(result.status_code, 504);
    EXPECT_EQ(result.downstream_service, "user_service");
    EXPECT_EQ(result.downstream_status, grpc::StatusCode::DEADLINE_EXCEEDED);
    EXPECT_GE(elapsed_ms, 80);
    EXPECT_LT(elapsed_ms, 400);

    bool cancelled_seen = false;
    for (int i = 0; i < 50; ++i) {
        if (cancel_aware_service.cancellation_observed()) {
            cancelled_seen = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_TRUE(cancelled_seen);
    EXPECT_EQ(cancel_aware_service.request_count(), 1);

    server->Shutdown();
}

TEST(GatewayIntegrationTest, WsHandshakeRejectsMissingAuthorization) {
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
    auto endpoints = resolver.resolve("127.0.0.1", port);
    net::connect(ws.next_layer(), endpoints);

    http::response<http::string_body> response;
    boost::system::error_code ec;
    ws.handshake(response, "127.0.0.1", "/", ec);
    EXPECT_TRUE(ec);
    EXPECT_EQ(response.result(), http::status::unauthorized);
    EXPECT_NE(response.body().find("unauthorized"), std::string::npos);

    if (ws.is_open()) {
        ws.close(websocket::close_code::normal);
    }
    http_server.Stop();
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
