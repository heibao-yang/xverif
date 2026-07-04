#pragma once

#include "../common/clock_sampling.h"

#include <string>

namespace xdebug_waveform {

struct ApbConfig {
    std::string name;
    std::string paddr;
    std::string pwdata;
    std::string prdata;
    std::string pwrite;
    std::string penable;
    std::string psel;
    std::string pready;
    std::string pslverr;
    ClockSampleSpec clock_sample;
    std::string rst_n;
};

} // namespace xdebug_waveform
