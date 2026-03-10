#include "framework/governance/rate_limiter.h"

namespace kd39::framework::governance {

TokenBucketRateLimiter::TokenBucketRateLimiter(double rate_per_second, double burst_capacity)
    : tokens_(burst_capacity),
      rate_per_second_(rate_per_second),
      burst_capacity_(burst_capacity),
      last_refill_(std::chrono::steady_clock::now()) {}

bool TokenBucketRateLimiter::Allow() {
    std::scoped_lock lock(mu_);
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(now - last_refill_).count();
    tokens_ = std::min(burst_capacity_, tokens_ + elapsed * rate_per_second_);
    last_refill_ = now;
    if (tokens_ < 1.0) {
        return false;
    }
    tokens_ -= 1.0;
    return true;
}

}  // namespace kd39::framework::governance
