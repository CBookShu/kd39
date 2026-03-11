#include "common/log/logger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace kd39::common::log {
namespace {

constexpr std::size_t kDefaultMaxFileSizeMb = 100;
constexpr std::size_t kDefaultMaxFiles = 10;
constexpr std::size_t kBytesPerMb = 1024 * 1024;
constexpr char kDefaultPattern[] = "[%Y-%m-%d %H:%M:%S.%e] [%t] [%^%l%$] [%s:%#] [%n] %v";
constexpr char kDefaultServiceName[] = "kd39";

std::once_flag g_init_once;
std::shared_ptr<spdlog::logger> g_default_logger;
std::atomic<spdlog::logger*> g_default_logger_raw{nullptr};
LoggerOptions g_init_options;
std::atomic<bool> g_conflict_warned{false};

std::string ToLowerAscii(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

std::size_t UseOrDefault(std::size_t value, std::size_t fallback) {
    return value == 0 ? fallback : value;
}

LoggerOptions NormalizeOptions(const LoggerOptions& options) {
    LoggerOptions normalized = options;
    if (normalized.service_name.empty()) {
        normalized.service_name = kDefaultServiceName;
    }
    if (normalized.log_dir.empty()) {
        normalized.log_dir = "logs";
    }
    normalized.max_file_size_mb = UseOrDefault(normalized.max_file_size_mb, kDefaultMaxFileSizeMb);
    normalized.max_files = UseOrDefault(normalized.max_files, kDefaultMaxFiles);
    if (normalized.level.empty()) {
        normalized.level = "info";
    }
    return normalized;
}

bool IsSameOptions(const LoggerOptions& lhs, const LoggerOptions& rhs) {
    return lhs.service_name == rhs.service_name &&
           lhs.log_dir == rhs.log_dir &&
           lhs.max_file_size_mb == rhs.max_file_size_mb &&
           lhs.max_files == rhs.max_files &&
           lhs.level == rhs.level &&
           lhs.enable_console == rhs.enable_console;
}

class DailyRotatingFileSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    DailyRotatingFileSink(std::filesystem::path log_dir,
                          std::string service_name,
                          std::size_t max_file_size_bytes,
                          std::size_t max_files)
        : log_dir_(std::move(log_dir)),
          service_name_(std::move(service_name)),
          max_file_size_bytes_(UseOrDefault(max_file_size_bytes, kDefaultMaxFileSizeMb * kBytesPerMb)),
          max_files_(UseOrDefault(max_files, kDefaultMaxFiles)) {
        std::filesystem::create_directories(log_dir_);
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        const auto day = DateString(msg.time);
        EnsureSinkForDay(day);
        if (active_sink_) {
            active_sink_->log(msg);
        }
    }

    void flush_() override {
        if (active_sink_) {
            active_sink_->flush();
        }
    }

    void set_pattern_(const std::string& pattern) override {
        spdlog::sinks::base_sink<std::mutex>::set_pattern_(pattern);
        if (active_sink_) {
            active_sink_->set_pattern(pattern);
        }
    }

    void set_formatter_(std::unique_ptr<spdlog::formatter> sink_formatter) override {
        spdlog::sinks::base_sink<std::mutex>::set_formatter_(std::move(sink_formatter));
        if (active_sink_ && this->formatter_) {
            active_sink_->set_formatter(this->formatter_->clone());
        }
    }

private:
    static std::string DateString(const spdlog::log_clock::time_point& time) {
        const auto time_t = spdlog::log_clock::to_time_t(time);
        std::tm tm_snapshot{};
#ifdef _WIN32
        localtime_s(&tm_snapshot, &time_t);
#else
        localtime_r(&time_t, &tm_snapshot);
#endif
        char buffer[16];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm_snapshot);
        return std::string(buffer);
    }

    std::filesystem::path BuildFilePath(const std::string& day) const {
        return log_dir_ / (service_name_ + "-" + day + ".log");
    }

    void EnsureSinkForDay(const std::string& day) {
        if (active_sink_ && day == active_day_) {
            return;
        }

        auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            BuildFilePath(day).string(), max_file_size_bytes_, max_files_, true);
        if (this->formatter_) {
            sink->set_formatter(this->formatter_->clone());
        }

        active_sink_ = std::move(sink);
        active_day_ = day;
    }

    std::filesystem::path log_dir_;
    std::string service_name_;
    std::size_t max_file_size_bytes_;
    std::size_t max_files_;
    std::string active_day_;
    std::shared_ptr<spdlog::sinks::rotating_file_sink_mt> active_sink_;
};

void EnsureInitialized(const LoggerOptions& options) {
    const auto normalized = NormalizeOptions(options);
    std::call_once(g_init_once, [normalized]() {
        const auto level = ParseLogLevel(normalized.level).value_or(spdlog::level::info);
        std::vector<spdlog::sink_ptr> sinks;
        if (normalized.enable_console) {
            sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        }

        std::string file_sink_error;
        try {
            auto daily_sink = std::make_shared<DailyRotatingFileSink>(
                std::filesystem::path(normalized.log_dir),
                normalized.service_name,
                normalized.max_file_size_mb * kBytesPerMb,
                normalized.max_files);
            sinks.emplace_back(std::move(daily_sink));
        } catch (const std::exception& ex) {
            file_sink_error = ex.what();
        }

        if (sinks.empty()) {
            sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        }

        auto logger = std::make_shared<spdlog::logger>(normalized.service_name, sinks.begin(), sinks.end());
        logger->set_pattern(kDefaultPattern);
        logger->set_level(level);
        logger->flush_on(spdlog::level::info);

        spdlog::set_default_logger(logger);
        spdlog::set_level(level);

        g_init_options = normalized;
        g_default_logger = logger;
        g_default_logger_raw.store(logger.get(), std::memory_order_release);

        if (!file_sink_error.empty()) {
            logger->warn("file log sink init failed, fallback to console only: {}", file_sink_error);
        }
        if (!ParseLogLevel(normalized.level).has_value()) {
            logger->warn("invalid log level '{}', fallback to '{}'", normalized.level, LogLevelName(level));
        }
    });
}

void EnsureDefaultInitialized() {
    EnsureInitialized(LoggerOptions{});
}

}  // namespace

void InitLogger(const LoggerOptions& options) {
    const auto normalized = NormalizeOptions(options);
    EnsureInitialized(normalized);

    if (!IsSameOptions(g_init_options, normalized) && !g_conflict_warned.exchange(true)) {
        Raw()->warn(
            "InitLogger called multiple times with different options; keeping first config service='{}' dir='{}' level='{}'",
            g_init_options.service_name,
            g_init_options.log_dir,
            g_init_options.level);
    }
}

void InitLogger(const std::string& service_name) {
    LoggerOptions options;
    options.service_name = service_name;
    InitLogger(options);
}

std::shared_ptr<spdlog::logger> Get() {
    EnsureDefaultInitialized();
    return g_default_logger;
}

spdlog::logger* Raw() {
    if (auto* logger = g_default_logger_raw.load(std::memory_order_acquire); logger != nullptr) {
        return logger;
    }
    EnsureDefaultInitialized();
    return g_default_logger_raw.load(std::memory_order_acquire);
}

std::optional<spdlog::level::level_enum> ParseLogLevel(std::string_view level_name) {
    const auto normalized = ToLowerAscii(level_name);
    if (normalized == "trace") {
        return spdlog::level::trace;
    }
    if (normalized == "debug") {
        return spdlog::level::debug;
    }
    if (normalized == "info") {
        return spdlog::level::info;
    }
    if (normalized == "warn" || normalized == "warning") {
        return spdlog::level::warn;
    }
    if (normalized == "error" || normalized == "err") {
        return spdlog::level::err;
    }
    if (normalized == "critical") {
        return spdlog::level::critical;
    }
    if (normalized == "off") {
        return spdlog::level::off;
    }
    return std::nullopt;
}

std::string_view LogLevelName(spdlog::level::level_enum level) {
    switch (level) {
        case spdlog::level::trace:
            return "trace";
        case spdlog::level::debug:
            return "debug";
        case spdlog::level::info:
            return "info";
        case spdlog::level::warn:
            return "warn";
        case spdlog::level::err:
            return "error";
        case spdlog::level::critical:
            return "critical";
        case spdlog::level::off:
            return "off";
        default:
            return "info";
    }
}

bool SetLogLevel(const std::string& level_name) {
    auto parsed = ParseLogLevel(level_name);
    if (!parsed.has_value()) {
        return false;
    }
    SetLogLevel(*parsed);
    return true;
}

void SetLogLevel(spdlog::level::level_enum level) {
    auto* logger = Raw();
    if (logger != nullptr) {
        logger->set_level(level);
    }
    spdlog::set_level(level);
}

spdlog::level::level_enum GetLogLevel() {
    auto* logger = Raw();
    if (logger != nullptr) {
        return logger->level();
    }
    return spdlog::level::info;
}

}  // namespace kd39::common::log
