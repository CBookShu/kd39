#pragma once

#include <chrono>
#include <mutex>

namespace kd39::framework::governance {

class TokenBucketRateLimiter {
public:
    TokenBucketRateLimiter(double rate_per_second, double burst_capacity);
    bool Allow();

private:
    std::mutex mu_;
    double tokens_;
    double rate_per_second_;
    double burst_capacity_;
    std::chrono::steady_clock::time_point last_refill_;
};

}  // namespace kd39::framework::governance
