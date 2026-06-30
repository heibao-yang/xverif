#pragma once

#include "common/env_config.h"

namespace xdebug_core {

// Centralized timeout configuration for session transport.
// Both design and waveform engines share the same env vars and defaults.

inline int file_transport_request_timeout_ms() {
    return xdebug_file_transport_timeout_ms();
}

inline int file_transport_ping_timeout_ms() {
    return xdebug_file_transport_ping_timeout_ms();
}

} // namespace xdebug_core
