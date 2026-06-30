#include "common/env_config.h"

#include <cassert>
#include <cstdlib>
#include <string>

namespace {

void reset_env() {
    unsetenv("XDEBUG_TRACE_SOURCE_CONTEXT_LINES");
    unsetenv("XDEBUG_TRACE_SOURCE_MERGE_THRESHOLD_LINES");
    unsetenv("XDEBUG_DEBUG");
    unsetenv("XDEBUG_SESSION_START_TIMEOUT_SEC");
}

} // namespace

int main() {
    reset_env();

    assert(xdebug_core::xdebug_trace_source_context_lines() == 3);
    assert(xdebug_core::xdebug_trace_source_merge_threshold_lines() == 10);
    assert(!xdebug_core::xdebug_debug_enabled());

    assert(setenv("XDEBUG_TRACE_SOURCE_CONTEXT_LINES", "2", 1) == 0);
    assert(setenv("XDEBUG_TRACE_SOURCE_MERGE_THRESHOLD_LINES", "6", 1) == 0);
    assert(xdebug_core::xdebug_trace_source_context_lines() == 2);
    assert(xdebug_core::xdebug_trace_source_merge_threshold_lines() == 6);

    assert(setenv("XDEBUG_DEBUG", "on", 1) == 0);
    assert(xdebug_core::xdebug_debug_enabled());
    assert(setenv("XDEBUG_DEBUG", "off", 1) == 0);
    assert(!xdebug_core::xdebug_debug_enabled());

    int value = 0;
    std::string error;
    assert(setenv("XDEBUG_SESSION_START_TIMEOUT_SEC", "11", 1) == 0);
    assert(xdebug_core::env_int("XDEBUG_SESSION_START_TIMEOUT_SEC", 300, 1, 1000, value, error));
    assert(value == 11);

    assert(setenv("XDEBUG_SESSION_START_TIMEOUT_SEC", "0", 1) == 0);
    error.clear();
    assert(!xdebug_core::env_int("XDEBUG_SESSION_START_TIMEOUT_SEC", 300, 1, 1000, value, error));
    assert(error.find("XDEBUG_SESSION_START_TIMEOUT_SEC") != std::string::npos);

    assert(setenv("XDEBUG_SESSION_START_TIMEOUT_SEC", "abc", 1) == 0);
    error.clear();
    assert(!xdebug_core::env_int("XDEBUG_SESSION_START_TIMEOUT_SEC", 300, 1, 1000, value, error));
    assert(error.find("XDEBUG_SESSION_START_TIMEOUT_SEC") != std::string::npos);

    reset_env();
    return 0;
}
