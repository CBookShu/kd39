#include "framework/governance/retry_policy.h"

namespace kd39::framework::governance {

RetryPolicy::RetryPolicy(int max_attempts, std::chrono::milliseconds backoff)
    : max_attempts_(max_attempts), backoff_(backoff) {}

}  // namespace kd39::framework::governance
