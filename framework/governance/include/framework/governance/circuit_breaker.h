#pragma once

#include <chrono>
#include <mutex>

namespace kd39::framework::governance {

class CircuitBreaker {
public:
    explicit CircuitBreaker(int failure_threshold = 5,
                            std::chrono::seconds cool_down = std::chrono::seconds(10));

    bool AllowRequest();
    void RecordSuccess();
    void RecordFailure();

private:
    enum class State { kClosed, kOpen, kHalfOpen };

    std::mutex mu_;
    State state_ = State::kClosed;
    int failure_threshold_ = 5;
    int failure_count_ = 0;
    std::chrono::seconds cool_down_{10};
    std::chrono::steady_clock::time_point opened_at_{};
};

}  // namespace kd39::framework::governance
