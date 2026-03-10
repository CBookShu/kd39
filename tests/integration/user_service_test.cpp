#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include "user_service_impl.h"
#include "user/user_service.grpc.pb.h"
#include "infrastructure/storage/mysql/connection_pool.h"
#include "infrastructure/storage/redis/redis_client.h"

TEST(UserServiceIntegrationTest, CreateThenGetUser) {
    auto mysql = kd39::infrastructure::storage::mysql::ConnectionPool::Create({"127.0.0.1", 3306, "root", "", "kd39", 2});
    auto redis = kd39::infrastructure::storage::redis::RedisClient::Create({"127.0.0.1", 6379, "", 0, 2});
    kd39::services::user::UserServiceImpl service({mysql, redis});

    int selected_port = 0;
    grpc::ServerBuilder builder;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &selected_port);
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();
    ASSERT_TRUE(server);

    auto channel = grpc::CreateChannel("127.0.0.1:" + std::to_string(selected_port), grpc::InsecureChannelCredentials());
    auto stub = kd39::api::user::UserService::NewStub(channel);

    grpc::ClientContext create_ctx;
    kd39::api::user::CreateUserRequest create_req;
    create_req.set_nickname("tester");
    kd39::api::user::CreateUserResponse create_resp;
    auto create_status = stub->CreateUser(&create_ctx, create_req, &create_resp);
    ASSERT_TRUE(create_status.ok());
    ASSERT_FALSE(create_resp.profile().user_id().empty());

    grpc::ClientContext get_ctx;
    kd39::api::user::GetUserRequest get_req;
    get_req.set_user_id(create_resp.profile().user_id());
    kd39::api::user::GetUserResponse get_resp;
    auto get_status = stub->GetUser(&get_ctx, get_req, &get_resp);
    ASSERT_TRUE(get_status.ok());
    EXPECT_EQ(get_resp.profile().nickname(), "tester");

    server->Shutdown();
}
