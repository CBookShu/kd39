#pragma once

#include <chrono>
#include <memory>
#include <string>

namespace kd39::infrastructure::coordination {

class DistributedLock {
public:
    virtual ~DistributedLock() = default;

    virtual bool TryLock(const std::string& resource,
                         std::chrono::seconds ttl) = 0;
    virtual void Unlock(const std::string& resource) = 0;
};

}  // namespace kd39::infrastructure::coordination
