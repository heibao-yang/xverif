#pragma once

#include "common/env_config.h"

#include <string>

namespace xdebug_core {

inline bool session_start_timeout_sec(int& value, std::string& error) {
    value = xdebug_session_start_timeout_sec(error);
    return value > 0;
}

inline bool session_idle_timeout_sec(int& value, std::string& error) {
    value = xdebug_session_idle_timeout_sec(error);
    return value > 0;
}

}  // namespace xdebug_core
