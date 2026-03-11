#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <spdlog/spdlog.h>

namespace kd39::common::log {

struct LoggerOptions {
    std::string service_name;
    std::string log_dir = "logs";
    std::size_t max_file_size_mb = 100;
    std::size_t max_files = 10;
    std::string level = "info";
    bool enable_console = true;
};

// Process-wide one-time logger initialization.
// The first successful InitLogger call wins; later calls keep the existing logger.
void InitLogger(const LoggerOptions& options);
void InitLogger(const std::string& service_name);
std::shared_ptr<spdlog::logger> Get();
spdlog::logger* Raw();

std::optional<spdlog::level::level_enum> ParseLogLevel(std::string_view level_name);
std::string_view LogLevelName(spdlog::level::level_enum level);
bool SetLogLevel(const std::string& level_name);
void SetLogLevel(spdlog::level::level_enum level);
spdlog::level::level_enum GetLogLevel();

}  // namespace kd39::common::log

#define KD39_LOG_TRACE(...) SPDLOG_LOGGER_CALL(::kd39::common::log::Raw(), ::spdlog::level::trace, __VA_ARGS__)
#define KD39_LOG_DEBUG(...) SPDLOG_LOGGER_CALL(::kd39::common::log::Raw(), ::spdlog::level::debug, __VA_ARGS__)
#define KD39_LOG_INFO(...) SPDLOG_LOGGER_CALL(::kd39::common::log::Raw(), ::spdlog::level::info, __VA_ARGS__)
#define KD39_LOG_WARN(...) SPDLOG_LOGGER_CALL(::kd39::common::log::Raw(), ::spdlog::level::warn, __VA_ARGS__)
#define KD39_LOG_ERROR(...) SPDLOG_LOGGER_CALL(::kd39::common::log::Raw(), ::spdlog::level::err, __VA_ARGS__)
#define KD39_LOG_CRITICAL(...) SPDLOG_LOGGER_CALL(::kd39::common::log::Raw(), ::spdlog::level::critical, __VA_ARGS__)
