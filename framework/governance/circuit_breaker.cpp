#include "framework/governance/circuit_breaker.h"

namespace kd39::framework::governance {

CircuitBreaker::CircuitBreaker(int failure_threshold, std::chrono::seconds cool_down)
    : failure_threshold_(failure_threshold), cool_down_(cool_down) {}

bool CircuitBreaker::AllowRequest() {
    std::scoped_lock lock(mu_);
    if (state_ == State::kClosed || state_ == State::kHalfOpen) {
        return true;
    }
    if (std::chrono::steady_clock::now() - opened_at_ >= cool_down_) {
        state_ = State::kHalfOpen;
        return true;
    }
    return false;
}

void CircuitBreaker::RecordSuccess() {
    std::scoped_lock lock(mu_);
    failure_count_ = 0;
    state_ = State::kClosed;
}

void CircuitBreaker::RecordFailure() {
    std::scoped_lock lock(mu_);
    ++failure_count_;
    if (failure_count_ >= failure_threshold_) {
        state_ = State::kOpen;
        opened_at_ = std::chrono::steady_clock::now();
    }
}

}  // namespace kd39::framework::governance
