#pragma once

#include "../common/clock_sampling.h"

#include <map>
#include <string>

namespace xdebug_waveform {

struct EventField {
    std::string signal_alias;
    int left = 0;
    int right = 0;
};

struct EventConfig {
    std::string name;
    ClockSampleSpec clock_sample;
    std::string rst_n;
    std::map<std::string, std::string> signals;
    std::map<std::string, EventField> fields;
};

} // namespace xdebug_waveform
