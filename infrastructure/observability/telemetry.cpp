#include "infrastructure/observability/telemetry.h"

#include "common/log/logger.h"

namespace kd39::infrastructure::observability {

Span::Span(std::string name) : name_(std::move(name)) {
    KD39_LOG_DEBUG("span start: {}", name_);
}

Span::~Span() {
    KD39_LOG_DEBUG("span end: {}", name_);
}

Tracer& Tracer::Instance() {
    static Tracer tracer;
    return tracer;
}

std::unique_ptr<Span> Tracer::StartSpan(const std::string& name) {
    return std::make_unique<Span>(name);
}

}  // namespace kd39::infrastructure::observability
