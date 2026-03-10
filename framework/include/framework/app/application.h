#pragma once

#include <functional>
#include <string>
#include <vector>

namespace kd39::framework {

class Application {
public:
    explicit Application(std::string name);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void AddShutdownHook(std::function<void()> hook);

    // Blocks until a termination signal is received, then runs shutdown hooks.
    void Run();

    const std::string& name() const { return name_; }

private:
    std::string name_;
    std::vector<std::function<void()>> shutdown_hooks_;
};

}  // namespace kd39::framework
