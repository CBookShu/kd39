#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kd39::common {

enum class ErrorCode : int32_t {
    kOk               = 0,
    kUnknown           = 1,
    kInvalidArgument   = 2,
    kNotFound          = 3,
    kAlreadyExists     = 4,
    kPermissionDenied  = 5,
    kUnauthenticated   = 6,
    kInternal          = 7,
    kUnavailable       = 8,
    kTimeout           = 9,
};

std::string_view ErrorCodeToString(ErrorCode code);

}  // namespace kd39::common
