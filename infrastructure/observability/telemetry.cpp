#include "infrastructure/observability/telemetry.h"

#include <spdlog/spdlog.h>

namespace kd39::infrastructure::observability {

Span::Span(std::string name) : name_(std::move(name)) {
    spdlog::debug("span start: {}", name_);
}

Span::~Span() {
    spdlog::debug("span end: {}", name_);
}

Tracer& Tracer::Instance() {
    static Tracer tracer;
    return tracer;
}

std::unique_ptr<Span> Tracer::StartSpan(const std::string& name) {
    return std::make_unique<Span>(name);
}

}  // namespace kd39::infrastructure::observability
