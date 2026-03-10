#pragma once

#include <cstdint>
#include <string>

namespace kd39::common {

struct RequestContext {
    std::string request_id;
    std::string trace_id;
    std::string user_id;
    std::string traffic_tag;
    std::string client_version;
    std::string zone;

    static constexpr const char* kMetaRequestId     = "x-request-id";
    static constexpr const char* kMetaTraceId       = "x-trace-id";
    static constexpr const char* kMetaUserId        = "x-user-id";
    static constexpr const char* kMetaTrafficTag    = "x-traffic-tag";
    static constexpr const char* kMetaClientVersion = "x-client-version";
    static constexpr const char* kMetaZone          = "x-zone";

    [[nodiscard]] bool empty() const {
        return request_id.empty() && trace_id.empty() && user_id.empty() &&
               traffic_tag.empty() && client_version.empty() && zone.empty();
    }
};

}  // namespace kd39::common
