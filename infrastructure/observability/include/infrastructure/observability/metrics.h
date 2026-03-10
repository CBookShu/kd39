#pragma once

#include <cstdint>
#include <string>

namespace kd39::infrastructure::observability {

class MetricsRegistry {
public:
    static MetricsRegistry& Instance();
    void Increment(const std::string& name, std::uint64_t delta = 1);
    std::string RenderPrometheus() const;
};

}  // namespace kd39::infrastructure::observability
