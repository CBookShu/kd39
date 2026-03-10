#include "common/context/context_store.h"

#include <optional>

namespace kd39::common {
namespace {
thread_local std::optional<RequestContext> g_request_context;
}

bool HasCurrentRequestContext() {
    return g_request_context.has_value();
}

RequestContext GetCurrentRequestContext() {
    return g_request_context.value_or(RequestContext{});
}

void SetCurrentRequestContext(const RequestContext& ctx) {
    g_request_context = ctx;
}

void ClearCurrentRequestContext() {
    g_request_context.reset();
}

ScopedRequestContext::ScopedRequestContext(const RequestContext& ctx) {
    had_previous_ = g_request_context.has_value();
    previous_ = g_request_context.value_or(RequestContext{});
    g_request_context = ctx;
}

ScopedRequestContext::~ScopedRequestContext() {
    if (had_previous_) {
        g_request_context = previous_;
    } else {
        g_request_context.reset();
    }
}

}  // namespace kd39::common
