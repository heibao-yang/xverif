#include "stream_config.h"

#include <cctype>
#include <set>

namespace xdebug_waveform {

namespace {

bool get_string(const Json& obj, const char* key, std::string& out) {
    auto it = obj.find(key);
    if (it == obj.end()) return false;
    if (!it->is_string()) return false;
    out = it->get<std::string>();
    return true;
}

bool is_ident_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool is_ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

} // namespace

bool stream_name_valid(const std::string& name) {
    if (name.empty() || !is_ident_start(name[0])) return false;
    for (char c : name) {
        if (!is_ident_char(c)) return false;
    }
    return true;
}

bool stream_field_name_valid(const std::string& name) {
    static const std::set<std::string> reserved = {
        "time", "cycle", "vld", "rdy", "bp", "sop", "eop",
        "transfer", "stall", "packet_index", "beat_index"
    };
    return stream_name_valid(name) && reserved.find(name) == reserved.end();
}

bool parse_stream_config_json(const Json& item, StreamConfig& config, std::string& error) {
    if (!item.is_object()) {
        error = "stream config must be an object";
        return false;
    }
    config = StreamConfig();
    get_string(item, "name", config.name);
    get_string(item, "clock", config.clock);
    get_string(item, "reset", config.reset);
    get_string(item, "vld", config.vld);
    get_string(item, "rdy", config.rdy);
    get_string(item, "bp", config.bp);
    get_string(item, "sop", config.sop);
    get_string(item, "eop", config.eop);
    get_string(item, "data", config.data);
    get_string(item, "channel_id", config.channel_id);
    get_string(item, "description", config.description);

    std::string edge;
    if (!get_string(item, "clock_edge", edge)) get_string(item, "edge", edge);
    if (edge.empty() || edge == "posedge") config.posedge = true;
    else if (edge == "negedge") config.posedge = false;
    else {
        error = "invalid clock_edge for stream " + config.name + ": " + edge;
        return false;
    }

    if (!stream_name_valid(config.name)) {
        error = "invalid stream name: " + config.name;
        return false;
    }
    if (config.clock.empty()) {
        error = "stream " + config.name + " requires clock";
        return false;
    }
    if (config.vld.empty()) {
        error = "stream " + config.name + " requires vld";
        return false;
    }

    auto fields_it = item.find("data_fields");
    if (fields_it != item.end()) {
        if (!fields_it->is_object()) {
            error = "stream " + config.name + " data_fields must be an object";
            return false;
        }
        for (auto it = fields_it->begin(); it != fields_it->end(); ++it) {
            if (!stream_field_name_valid(it.key())) {
                error = "invalid or reserved data field name: " + it.key();
                return false;
            }
            if (!it.value().is_string() || it.value().get<std::string>().empty()) {
                error = "data field " + it.key() + " must map to a non-empty expression";
                return false;
            }
            config.data_fields[it.key()] = it.value().get<std::string>();
        }
    }
    if (config.data.empty() && config.data_fields.empty()) {
        error = "stream " + config.name + " requires data or data_fields";
        return false;
    }
    if ((config.sop.empty()) != (config.eop.empty())) {
        error = "stream " + config.name + " requires sop and eop to be configured together";
        return false;
    }
    return true;
}

Json stream_config_json(const StreamConfig& c) {
    Json j;
    j["name"] = c.name;
    j["clock"] = c.clock;
    j["clock_edge"] = c.posedge ? "posedge" : "negedge";
    if (!c.reset.empty()) j["reset"] = c.reset;
    j["vld"] = c.vld;
    if (!c.rdy.empty()) j["rdy"] = c.rdy;
    if (!c.bp.empty()) j["bp"] = c.bp;
    if (!c.sop.empty()) j["sop"] = c.sop;
    if (!c.eop.empty()) j["eop"] = c.eop;
    if (!c.data.empty()) j["data"] = c.data;
    if (!c.data_fields.empty()) {
        j["data_fields"] = Json::object();
        for (const auto& kv : c.data_fields) j["data_fields"][kv.first] = kv.second;
    }
    if (!c.channel_id.empty()) j["channel_id"] = c.channel_id;
    if (!c.description.empty()) j["description"] = c.description;
    return j;
}

std::string stream_handshake_text(const StreamConfig& c) {
    if (!c.rdy.empty() && !c.bp.empty()) return "vld/rdy/bp";
    if (!c.rdy.empty()) return "vld/rdy";
    if (!c.bp.empty()) return "vld/bp";
    return "vld";
}

bool stream_packet_enabled(const StreamConfig& c) {
    return !c.sop.empty() && !c.eop.empty();
}

} // namespace xdebug_waveform
