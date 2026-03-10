#include "common/error/error_code.h"
#include <gtest/gtest.h>

using kd39::common::ErrorCode;
using kd39::common::ErrorCodeToString;

TEST(ErrorCodeTest, KnownCodes) {
    EXPECT_EQ(ErrorCodeToString(ErrorCode::kOk), "OK");
    EXPECT_EQ(ErrorCodeToString(ErrorCode::kNotFound), "NOT_FOUND");
    EXPECT_EQ(ErrorCodeToString(ErrorCode::kInternal), "INTERNAL");
}
