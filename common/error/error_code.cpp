#include "common/error/error_code.h"

namespace kd39::common {

std::string_view ErrorCodeToString(ErrorCode code) {
    switch (code) {
        case ErrorCode::kOk:               return "OK";
        case ErrorCode::kUnknown:          return "UNKNOWN";
        case ErrorCode::kInvalidArgument:  return "INVALID_ARGUMENT";
        case ErrorCode::kNotFound:         return "NOT_FOUND";
        case ErrorCode::kAlreadyExists:    return "ALREADY_EXISTS";
        case ErrorCode::kPermissionDenied: return "PERMISSION_DENIED";
        case ErrorCode::kUnauthenticated:  return "UNAUTHENTICATED";
        case ErrorCode::kInternal:         return "INTERNAL";
        case ErrorCode::kUnavailable:      return "UNAVAILABLE";
        case ErrorCode::kTimeout:          return "TIMEOUT";
    }
    return "UNKNOWN";
}

}  // namespace kd39::common
