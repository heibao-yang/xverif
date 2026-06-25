#pragma once

#include "../apb/apb_config.h"
#include "../axi/axi_config.h"
#include "../event/event_config.h"
#include "json.hpp"

#include <istream>
#include <string>

namespace xdebug_waveform {

using Json = nlohmann::ordered_json;

extern const char* kApiVersion;

bool load_text_file(const std::string& path, std::string& out);
std::string trim(std::string s);
std::string compact_expr_ws(const std::string& expr);
bool contains_xz(const std::string& value);
std::string normalize_numeric(std::string value);
Json make_value_object(const std::string& raw);
Json make_value_map(const Json& raw_map);
Json simplify_event_value_objects(Json events);
Json aggregate_events(const Json& events, const Json& aggregate_args, int limit);
bool get_string(const Json& obj, const char* key, std::string& out);
std::string string_or(const Json& obj, const char* key, const std::string& def);
int int_or(const Json& obj, const char* key, int def);
bool bool_or(const Json& obj, const char* key, bool def);

bool parse_apb_config(const Json& j, ApbConfig& c, std::string& err);
Json apb_config_json(const ApbConfig& c);
bool parse_axi_config(const Json& j, AxiConfig& c, std::string& err);
Json axi_config_json(const AxiConfig& c);
bool parse_event_config(const Json& j, EventConfig& c, std::string& err);
Json event_config_json(const EventConfig& c);
bool load_config_json_arg(const Json& args, Json& config, std::string& err);
std::string arg_text(const Json& v);

void print_schema();
bool server_ai_action(const std::string& action);
int print_error_and_return(const Json& req, const std::string& action,
                           const std::string& code, const std::string& msg, long long elapsed_ms);

} // namespace xdebug_waveform
