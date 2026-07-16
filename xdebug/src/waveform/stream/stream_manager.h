#pragma once

#include "stream_config.h"

#include <string>
#include <vector>

namespace xdebug_waveform {

struct StreamConfigChange {
    std::string name;
    std::string old_semantic_fingerprint;
    std::string new_semantic_fingerprint;
};

class StreamManager {
public:
    bool load_session(const std::string& session_id, std::vector<StreamConfig>& configs);
    bool save_session(const std::string& session_id, const std::vector<StreamConfig>& configs);
    bool load_configs(const std::string& session_id, const std::vector<StreamConfig>& incoming,
                      const std::string& mode, std::string& error,
                      std::vector<StreamConfigChange>* changes = nullptr);
    bool get_stream(const std::string& session_id, const std::string& name, StreamConfig& config);
    std::vector<StreamConfig> list_streams(const std::string& session_id);
};

bool load_stream_config_arg(const Json& args, Json& root, std::string& error);
bool parse_stream_config_list(const Json& root, std::vector<StreamConfig>& streams, std::string& error);

} // namespace xdebug_waveform
