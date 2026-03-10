#pragma once

#include "common/context/request_context.h"

namespace kd39::common {

bool HasCurrentRequestContext();
RequestContext GetCurrentRequestContext();
void SetCurrentRequestContext(const RequestContext& ctx);
void ClearCurrentRequestContext();

class ScopedRequestContext {
public:
    explicit ScopedRequestContext(const RequestContext& ctx);
    ~ScopedRequestContext();

    ScopedRequestContext(const ScopedRequestContext&) = delete;
    ScopedRequestContext& operator=(const ScopedRequestContext&) = delete;

private:
    bool had_previous_ = false;
    RequestContext previous_{};
};

}  // namespace kd39::common
