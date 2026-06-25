#include "action_support.h"

#include "../apb/apb_config.h"
#include "../apb/apb_manager.h"
#include "../axi/axi_config.h"
#include "../axi/axi_manager.h"
#include "../event/event_config.h"
#include "../event/event_manager.h"
#include "../list/list_manager.h"
#include "../protocol/protocol.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace xdebug_waveform {

const char* kApiVersion = "xdebug.internal.v1";

namespace {

std::string slurp_stream(std::istream& is) {
    std::ostringstream oss;
    oss << is.rdbuf();
    return oss.str();
}

} // namespace

bool load_text_file(const std::string& path, std::string& out) {
    std::ifstream ifs(path);
    if (!ifs) return false;
    out = slurp_stream(ifs);
    return true;
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

bool get_string(const Json& obj, const char* key, std::string& out) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) return false;
    out = it->get<std::string>();
    return true;
}

std::string string_or(const Json& obj, const char* key, const std::string& def) {
    std::string value;
    return get_string(obj, key, value) ? value : def;
}

int int_or(const Json& obj, const char* key, int def) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_number_integer()) return def;
    return it->get<int>();
}

bool bool_or(const Json& obj, const char* key, bool def) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_boolean()) return def;
    return it->get<bool>();
}

} // namespace xdebug_waveform
