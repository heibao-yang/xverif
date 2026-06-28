#pragma once

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>

namespace xdebug_core {

inline bool parse_positive_env_seconds(const char* name,
                                       int default_value,
                                       int& value,
                                       std::string& error) {
    const char* raw = std::getenv(name);
    if (!raw || !*raw) {
        value = default_value;
        return true;
    }

    errno = 0;
    char* end = nullptr;
    long parsed = std::strtol(raw, &end, 10);
    if (errno != 0 || end == raw || (end && *end != '\0') ||
        parsed <= 0 || parsed > INT_MAX) {
        error = std::string(name) + " must be a positive integer number of seconds";
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

inline bool session_start_timeout_sec(int& value, std::string& error) {
    return parse_positive_env_seconds("XDEBUG_SESSION_START_TIMEOUT_SEC", 300, value, error);
}

inline bool session_idle_timeout_sec(int& value, std::string& error) {
    return parse_positive_env_seconds("XDEBUG_SESSION_IDLE_TIMEOUT_SEC", 86400, value, error);
}

}  // namespace xdebug_core
