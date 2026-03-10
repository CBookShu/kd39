#include "infrastructure/coordination/impl/etcd/factory.h"

#include "infrastructure/coordination/impl/etcd/etcd_state.h"

namespace kd39::infrastructure::coordination::etcd {

class EtcdLeaderElection final : public LeaderElection {
public:
    explicit EtcdLeaderElection(std::string endpoints)
        : endpoints_(std::move(endpoints)) {}

    void Campaign(const std::string& election_name,
                  const std::string& candidate_id,
                  Callback on_status_change) override {
        auto state = detail::GetState(endpoints_);
        std::scoped_lock lock(state->mu);
        auto& leader = state->leaders[election_name];
        if (leader.empty()) {
            leader = candidate_id;
        }
        on_status_change(leader == candidate_id);
    }

    void Resign() override {}

private:
    std::string endpoints_;
};

std::shared_ptr<LeaderElection> CreateLeaderElection(const std::string& endpoints) {
    return std::make_shared<EtcdLeaderElection>(endpoints);
}

}  // namespace kd39::infrastructure::coordination::etcd
