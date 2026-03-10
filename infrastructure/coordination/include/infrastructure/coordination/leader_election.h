#pragma once

#include <functional>
#include <memory>
#include <string>

namespace kd39::infrastructure::coordination {

class LeaderElection {
public:
    virtual ~LeaderElection() = default;

    using Callback = std::function<void(bool is_leader)>;

    virtual void Campaign(const std::string& election_name,
                          const std::string& candidate_id,
                          Callback on_status_change) = 0;

    virtual void Resign() = 0;
};

}  // namespace kd39::infrastructure::coordination
