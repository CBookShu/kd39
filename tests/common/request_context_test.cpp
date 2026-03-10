#include "common/context/request_context.h"
#include <gtest/gtest.h>
#include <string>

using kd39::common::RequestContext;

TEST(RequestContextTest, MetadataKeyConstants) {
    EXPECT_EQ(std::string(RequestContext::kMetaRequestId), "x-request-id");
    EXPECT_EQ(std::string(RequestContext::kMetaTrafficTag), "x-traffic-tag");
}
