#include "infrastructure/observability/metrics.h"

#include <mutex>
#include <sstream>
#include <unordered_map>

namespace kd39::infrastructure::observability {
namespace {
std::mutex g_mu;
std::unordered_map<std::string, std::uint64_t> g_counters;
}

MetricsRegistry& MetricsRegistry::Instance() {
    static MetricsRegistry registry;
    return registry;
}

void MetricsRegistry::Increment(const std::string& name, std::uint64_t delta) {
    std::scoped_lock lock(g_mu);
    g_counters[name] += delta;
}

std::string MetricsRegistry::RenderPrometheus() const {
    std::ostringstream out;
    std::scoped_lock lock(g_mu);
    for (const auto& [name, value] : g_counters) {
        out << "# TYPE " << name << " counter\n";
        out << name << ' ' << value << "\n";
    }
    return out.str();
}

}  // namespace kd39::infrastructure::observability
