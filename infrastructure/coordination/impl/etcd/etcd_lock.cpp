#include "infrastructure/coordination/impl/etcd/factory.h"

#include "infrastructure/coordination/impl/etcd/etcd_state.h"

namespace kd39::infrastructure::coordination::etcd {

class EtcdDistributedLock final : public DistributedLock {
public:
    explicit EtcdDistributedLock(std::string endpoints)
        : endpoints_(std::move(endpoints)) {}

    bool TryLock(const std::string& resource, std::chrono::seconds ttl) override {
        auto state = detail::GetState(endpoints_);
        std::scoped_lock lock(state->mu);
        if (state->locks.contains(resource)) {
            return false;
        }
        state->locks[resource] = std::to_string(ttl.count());
        return true;
    }

    void Unlock(const std::string& resource) override {
        auto state = detail::GetState(endpoints_);
        std::scoped_lock lock(state->mu);
        state->locks.erase(resource);
    }

private:
    std::string endpoints_;
};

std::shared_ptr<DistributedLock> CreateDistributedLock(const std::string& endpoints) {
    return std::make_shared<EtcdDistributedLock>(endpoints);
}

}  // namespace kd39::infrastructure::coordination::etcd
