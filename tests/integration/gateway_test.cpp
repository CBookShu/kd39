#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "access_gateway/auth/auth_middleware.h"
#include "access_gateway/http/http_server.h"
#include "access_gateway/routing/grpc_router.h"
#include "user_service_impl.h"
#include "infrastructure/storage/mysql/connection_pool.h"
#include "infrastructure/storage/redis/redis_client.h"

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
