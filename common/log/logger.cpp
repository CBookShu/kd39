#include "common/log/logger.h"

#include <spdlog/sinks/stdout_color_sinks.h>

namespace kd39::common::log {

void InitLogger(const std::string& service_name) {
    auto logger = spdlog::get(service_name);
    if (!logger) {
        logger = spdlog::stdout_color_mt(service_name);
    }
    spdlog::set_default_logger(logger);
    spdlog::set_pattern(std::string("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [") + service_name + "] %v");
    spdlog::flush_on(spdlog::level::info);
}

std::shared_ptr<spdlog::logger> Get() {
    return spdlog::default_logger();
}

}  // namespace kd39::common::log
