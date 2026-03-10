#include "framework/app/application.h"

#include <spdlog/spdlog.h>
#include <csignal>
#include <atomic>
#include <condition_variable>
#include <mutex>

namespace {
std::atomic<bool> g_running{true};
std::mutex g_mu;
std::condition_variable g_cv;

void SignalHandler(int) {
    g_running.store(false);
    g_cv.notify_all();
}
}  // namespace

namespace kd39::framework {

Application::Application(std::string name) : name_(std::move(name)) {
    spdlog::info("[{}] initializing", name_);
}

Application::~Application() = default;

void Application::AddShutdownHook(std::function<void()> hook) {
    shutdown_hooks_.push_back(std::move(hook));
}

void Application::Run() {
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    spdlog::info("[{}] running – press Ctrl+C to stop", name_);

    {
        std::unique_lock lock(g_mu);
        g_cv.wait(lock, [] { return !g_running.load(); });
    }

    spdlog::info("[{}] shutting down ...", name_);
    for (auto it = shutdown_hooks_.rbegin(); it != shutdown_hooks_.rend(); ++it) {
        (*it)();
    }
    spdlog::info("[{}] stopped", name_);
}

}  // namespace kd39::framework
