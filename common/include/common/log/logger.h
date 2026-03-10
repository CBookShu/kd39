#pragma once

#include <memory>
#include <string>
#include <spdlog/spdlog.h>

namespace kd39::common::log {

void InitLogger(const std::string& service_name);
std::shared_ptr<spdlog::logger> Get();

}  // namespace kd39::common::log
