#pragma once

#include <memory>
#include <string>

namespace kd39::infrastructure::observability {

class Span {
public:
    explicit Span(std::string name);
    ~Span();

private:
    std::string name_;
};

class Tracer {
public:
    static Tracer& Instance();
    std::unique_ptr<Span> StartSpan(const std::string& name);
};

}  // namespace kd39::infrastructure::observability
