#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include "config/config_service.grpc.pb.h"
#include "config_service_impl.h"
#include "infrastructure/coordination/impl/etcd/factory.h"
#include "infrastructure/mq/producer.h"
#include "infrastructure/storage/mysql/connection_pool.h"
#include "infrastructure/storage/redis/redis_client.h"

TEST(ConfigServiceIntegrationTest, PublishThenGetConfig) {
    auto mysql = kd39::infrastructure::storage::mysql::ConnectionPool::Create({"127.0.0.1", 3306, "root", "", "kd39", 2});
    auto redis = kd39::infrastructure::storage::redis::RedisClient::Create({"127.0.0.1", 6379, "", 0, 2});
    auto provider = kd39::infrastructure::coordination::etcd::CreateConfigProvider("test-etcd");
    auto producer = kd39::infrastructure::mq::CreateRedisStreamsProducer("redis://test");
    kd39::services::config::ConfigServiceImpl service({provider, mysql, redis, producer});

    int selected_port = 0;
    grpc::ServerBuilder builder;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &selected_port);
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();
    ASSERT_TRUE(server);

    auto channel = grpc::CreateChannel("127.0.0.1:" + std::to_string(selected_port), grpc::InsecureChannelCredentials());
    auto stub = kd39::api::config::ConfigService::NewStub(channel);

    grpc::ClientContext publish_ctx;
    kd39::api::config::PublishConfigRequest publish_req;
    publish_req.mutable_entry()->set_namespace_name("feature_flags");
    publish_req.mutable_entry()->set_key("new_login");
    publish_req.mutable_entry()->set_value("true");
    publish_req.mutable_entry()->set_environment("dev");
    kd39::api::config::PublishConfigResponse publish_resp;
    auto publish_status = stub->PublishConfig(&publish_ctx, publish_req, &publish_resp);
    ASSERT_TRUE(publish_status.ok());
    EXPECT_GT(publish_resp.version(), 0);

    grpc::ClientContext get_ctx;
    kd39::api::config::GetConfigRequest get_req;
    get_req.set_namespace_name("feature_flags");
    get_req.set_key("new_login");
    kd39::api::config::GetConfigResponse get_resp;
    auto get_status = stub->GetConfig(&get_ctx, get_req, &get_resp);
    ASSERT_TRUE(get_status.ok());
    EXPECT_EQ(get_resp.entry().value(), "true");

    server->Shutdown();
}
