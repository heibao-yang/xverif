#pragma once

#include "../list/signal_list.h"
#include "json.hpp"
#include "npi_fsdb.h"

#include <cstdint>
#include <string>

namespace xdebug_waveform {

using Json = nlohmann::ordered_json;

struct ListExportOptions {
    std::string session_id;
    std::string list_name;
    std::string output_dir;
    std::string format = "u64bin";
    npiFsdbTime begin = 0;
    npiFsdbTime end = 0;
};

struct ListExportResult {
    std::string output_dir;
    std::string manifest_file;
    std::string format;
    size_t signal_count = 0;
    size_t row_count = 0;
    Json signals = Json::array();
};

bool export_signal_list(npiFsdbFileHandle file,
                        const SignalList& list,
                        const ListExportOptions& options,
                        ListExportResult& result,
                        std::string& error);

} // namespace xdebug_waveform
