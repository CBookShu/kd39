#include <future>
#include <gtest/gtest.h>

#include "infrastructure/mq/consumer.h"
#include "infrastructure/mq/producer.h"

TEST(MqIntegrationTest, ProducerConsumerRoundTrip) {
    auto producer = kd39::infrastructure::mq::CreateRedisStreamsProducer("redis://test");
    auto consumer = kd39::infrastructure::mq::CreateRedisStreamsConsumer("redis://test", "test-group", "test-consumer");

    std::promise<std::string> promise;
    auto future = promise.get_future();

    consumer->Subscribe("config.changed", [&promise](std::string_view payload) mutable {
        promise.set_value(std::string(payload));
    });
    consumer->Start();

    ASSERT_TRUE(producer->Publish("config.changed", R"({"key":"value"})"));
    const auto status = future.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready);
    EXPECT_EQ(future.get(), R"({"key":"value"})");

    consumer->Stop();
}
