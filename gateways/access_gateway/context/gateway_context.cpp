#include "access_gateway/context/gateway_context.h"

namespace kd39::gateways::access {

common::RequestContext BuildContextFromHeaders(
    const std::string& request_id,
    const std::string& trace_id,
    const std::string& traffic_tag,
    const std::string& client_version,
    const std::string& zone) {
    common::RequestContext ctx;
    ctx.request_id     = request_id;
    ctx.trace_id       = trace_id;
    ctx.traffic_tag    = traffic_tag;
    ctx.client_version = client_version;
    ctx.zone           = zone;
    return ctx;
}

}  // namespace kd39::gateways::access
