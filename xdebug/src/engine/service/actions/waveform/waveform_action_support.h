#pragma once

#include "waveform/common/xdebug_waveform_paths.h"
#include "waveform/list/signal_list.h"

#include <string>

namespace xdebug_design {

inline bool read_list_storage(const std::string& name,
                              xdebug_waveform::SignalList& lst) {
    return xdebug_waveform::read_list_from_storage(
        xdebug_waveform::g_session_id, name.c_str(), lst);
}

} // namespace xdebug_design
