#pragma once

#include "common/context/request_context.h"
#include <string>

namespace kd39::gateways::access {

// Builds a RequestContext from external HTTP headers.
common::RequestContext BuildContextFromHeaders(
    const std::string& request_id,
    const std::string& trace_id,
    const std::string& traffic_tag,
    const std::string& client_version,
    const std::string& zone);

}  // namespace kd39::gateways::access
