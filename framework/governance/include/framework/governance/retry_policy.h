#pragma once

#include <chrono>
#include <thread>

namespace kd39::framework::governance {

class RetryPolicy {
public:
    RetryPolicy(int max_attempts, std::chrono::milliseconds backoff);

    template <typename Fn>
    bool Run(Fn&& fn) const {
        for (int attempt = 1; attempt <= max_attempts_; ++attempt) {
            if (fn()) {
                return true;
            }
            if (attempt < max_attempts_) {
                std::this_thread::sleep_for(backoff_);
            }
        }
        return false;
    }

private:
    int max_attempts_ = 1;
    std::chrono::milliseconds backoff_{0};
};

}  // namespace kd39::framework::governance
