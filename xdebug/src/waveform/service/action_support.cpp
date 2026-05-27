#include "../commands/cmd_ai.h"

#include "../apb/apb_config.h"
#include "../apb/apb_manager.h"
#include "../axi/axi_config.h"
#include "../axi/axi_manager.h"
#include "../client/client.h"
#include "../event/event_config.h"
#include "../event/event_manager.h"
#include "json.hpp"
#include "../list/list_manager.h"
#include "../protocol/protocol.h"
#include "../session/session_manager.h"
#include "../session/session_registry.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

namespace xdebug_waveform {

using Json = nlohmann::ordered_json;

const char* kApiVersion = "xdebug.internal.v1";

std::string read_stream(std::istream& is) {
    std::ostringstream oss;
    oss << is.rdbuf();
    return oss.str();
}

bool read_file(const std::string& path, std::string& out) {
    std::ifstream ifs(path);
    if (!ifs) return false;
    out = read_stream(ifs);
    return true;
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

std::string compact_expr_ws(const std::string& expr) {
    std::string out;
    out.reserve(expr.size());
    for (char c : expr) {
        if (!std::isspace(static_cast<unsigned char>(c))) out.push_back(c);
    }
    return out;
}

bool contains_xz(const std::string& value) {
    std::string v = trim(value);
    size_t start = 0;
    if (v.size() >= 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) {
        start = 2;
    } else if (v.size() >= 2 && v[0] == '\'' &&
               (v[1] == 'h' || v[1] == 'H' || v[1] == 'b' || v[1] == 'B' ||
                v[1] == 'd' || v[1] == 'D')) {
        start = 2;
    }
    for (size_t i = start; i < v.size(); ++i) {
        char c = v[i];
        if (c == 'x' || c == 'X' || c == 'z' || c == 'Z') return true;
    }
    return false;
}

std::string normalize_numeric(std::string value) {
    value = trim(value);
    if (value.size() >= 2 && value[0] == '\'' && (value[1] == 'h' || value[1] == 'H')) {
        value = "0x" + value.substr(2);
    } else if (value.size() >= 2 && value[0] == '\'' && (value[1] == 'b' || value[1] == 'B')) {
        value = "0b" + value.substr(2);
    } else if (value.size() >= 2 && value[0] == '\'' && (value[1] == 'd' || value[1] == 'D')) {
        value = value.substr(2);
    }
    if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
        value = value.substr(2);
    } else if (value.size() > 2 && value[0] == '0' && (value[1] == 'b' || value[1] == 'B')) {
        unsigned long long n = strtoull(value.substr(2).c_str(), nullptr, 2);
        char buf[64];
        snprintf(buf, sizeof(buf), "%llx", n);
        value = buf;
    } else {
        bool decimal = !value.empty();
        for (char c : value) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                decimal = false;
                break;
            }
        }
        if (decimal) {
            unsigned long long n = strtoull(value.c_str(), nullptr, 10);
            char buf[64];
            snprintf(buf, sizeof(buf), "%llx", n);
            value = buf;
        }
    }
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    size_t first = value.find_first_not_of('0');
    if (first == std::string::npos) return "0";
    return value.substr(first);
}

Json make_value_object(const std::string& raw) {
    Json v;
    std::string text = trim(raw);
    v["value"] = text;
    v["known"] = !contains_xz(text);
    return v;
}

Json make_value_map(const Json& raw_map) {
    Json out = Json::object();
    if (!raw_map.is_object()) return out;
    for (auto it = raw_map.begin(); it != raw_map.end(); ++it) {
        if (it.value().is_string()) out[it.key()] = make_value_object(it.value().get<std::string>());
        else out[it.key()] = it.value();
    }
    return out;
}

Json simplify_event_value_objects(Json events) {
    if (!events.is_array()) return events;
    for (auto& ev : events) {
        if (!ev.is_object()) continue;
        if (ev.contains("signals")) ev["signals"] = make_value_map(ev["signals"]);
        if (ev.contains("fields")) ev["fields"] = make_value_map(ev["fields"]);
    }
    return events;
}

std::string event_group_value(const Json& ev, const std::string& key) {
    auto get = [&](const char* section) -> std::string {
        if (!ev.contains(section) || !ev[section].is_object()) return std::string();
        auto it = ev[section].find(key);
        if (it == ev[section].end()) return std::string();
        if (it->is_string()) return it->get<std::string>();
        if (it->is_object() && it->contains("value") && (*it)["value"].is_string()) return (*it)["value"].get<std::string>();
        return std::string();
    };
    std::string v = get("fields");
    if (v.empty()) v = get("signals");
    if (v.empty() || v == "?" || contains_xz(v)) return "unknown";
    return v;
}

Json aggregate_events(const Json& events, const Json& aggregate_args, int limit) {
    bool want_count = aggregate_args.value("count", true);
    Json group_by_json = aggregate_args.value("group_by", Json::array());
    std::vector<std::string> group_by;
    if (group_by_json.is_array()) {
        for (const auto& item : group_by_json) if (item.is_string()) group_by.push_back(item.get<std::string>());
    }

    Json out = Json::object();
    size_t event_count = events.is_array() ? events.size() : 0;
    if (want_count) out["count"] = event_count;
    out["limited"] = limit > 0 && event_count >= static_cast<size_t>(limit);

    if (!group_by.empty() && events.is_array()) {
        struct GroupState {
            Json key;
            int count = 0;
            std::string first_time;
            std::string last_time;
        };
        std::map<std::string, GroupState> groups;
        for (const auto& ev : events) {
            Json key_obj = Json::object();
            for (const auto& key : group_by) key_obj[key] = event_group_value(ev, key);
            std::string group_id = key_obj.dump();
            GroupState& st = groups[group_id];
            if (st.count == 0) {
                st.key = key_obj;
                st.first_time = ev.value("time", std::string());
            }
            st.count++;
            st.last_time = ev.value("time", std::string());
        }
        Json arr = Json::array();
        for (const auto& kv : groups) {
            arr.push_back({{"key", kv.second.key},
                           {"count", kv.second.count},
                           {"first_time", kv.second.first_time},
                           {"last_time", kv.second.last_time}});
        }
        out["groups"] = arr;
        out["group_count"] = arr.size();
    }
    return out;
}

Json base_response(const Json& req,
                          const std::string& action,
                          bool ok,
                          long long elapsed_ms) {
    Json out;
    out["api_version"] = kApiVersion;
    if (req.contains("request_id")) out["request_id"] = req["request_id"];
    out["ok"] = ok;
    out["action"] = action;
    out["tool"] = {{"name", "xdebug_waveform"}, {"version", "0.1.0"}};
    out["session"] = Json::object();
    out["summary"] = Json::object();
    out["data"] = ok ? Json::object() : Json(nullptr);
    out["findings"] = Json::array();
    out["suggested_next_actions"] = Json::array();
    out["warnings"] = Json::array();
    out["error"] = nullptr;
    out["meta"] = {{"elapsed_ms", elapsed_ms}, {"truncated", false}};
    return out;
}

Json error_response(const Json& req,
                           const std::string& action,
                           const std::string& code,
                           const std::string& message,
                           bool recoverable,
                           long long elapsed_ms) {
    Json out = base_response(req, action, false, elapsed_ms);
    out["error"] = {
        {"code", code},
        {"message", message},
        {"recoverable", recoverable},
        {"candidates", Json::array()},
        {"suggested_actions", Json::array()}
    };
    if (code == "SIGNAL_NOT_FOUND") {
        out["suggested_next_actions"].push_back({
            {"tool", "xdebug_waveform"},
            {"action", "scope.list"},
            {"reason", "exact signal was not found"}
        });
    }
    return out;
}

std::string response_verbosity(const Json& req, bool* valid = nullptr) {
    if (valid) *valid = true;
    Json output = req.value("output", Json::object());
    std::string verbosity = "compact";
    if (!output.is_object()) {
        if (valid) *valid = false;
        return "compact";
    }
    if (output.is_object()) {
        auto it = output.find("verbosity");
        if (it != output.end()) {
            if (!it->is_string()) {
                if (valid) *valid = false;
                return "compact";
            }
            verbosity = it->get<std::string>();
        }
    }
    if (verbosity.empty()) verbosity = "compact";
    std::transform(verbosity.begin(), verbosity.end(), verbosity.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (verbosity == "compact" || verbosity == "full" || verbosity == "debug") return verbosity;
    if (valid) *valid = false;
    return "compact";
}

bool json_empty_or_null(const Json& v) {
    return v.is_null() || (v.is_object() && v.empty()) || (v.is_array() && v.empty());
}

Json compact_session_json(const Json& full) {
    Json out;
    if (full.contains("id")) out["id"] = full["id"];
    if (full.contains("fsdb")) out["fsdb"] = full["fsdb"];
    return out;
}

Json compact_error_json(const Json& full) {
    if (!full.is_object()) return full;
    Json out;
    if (full.contains("code")) out["code"] = full["code"];
    if (full.contains("message")) out["message"] = full["message"];
    if (full.contains("recoverable")) out["recoverable"] = full["recoverable"];
    if (full.contains("candidates") && !json_empty_or_null(full["candidates"])) out["candidates"] = full["candidates"];
    if (full.contains("suggested_actions") && !json_empty_or_null(full["suggested_actions"])) {
        out["suggested_actions"] = full["suggested_actions"];
    }
    return out;
}

Json compact_response(const Json& full) {
    Json out;
    if (full.contains("request_id")) out["request_id"] = full["request_id"];
    out["ok"] = full.value("ok", false);
    out["action"] = full.value("action", std::string());

    bool ok = out["ok"].get<bool>();
    if (full.contains("summary") && !json_empty_or_null(full["summary"])) out["summary"] = full["summary"];

    if (full.contains("data") && !json_empty_or_null(full["data"])) {
        out["data"] = full["data"];
        std::string action = out.value("action", std::string());
        if ((action == "session.open" || action == "session.list") && out["data"].is_object()) {
            if (out["data"].contains("session")) {
                out["data"]["session"] = compact_session_json(out["data"]["session"]);
            }
            if (out["data"].contains("sessions") && out["data"]["sessions"].is_array()) {
                Json sessions = Json::array();
                for (const auto& s : out["data"]["sessions"]) sessions.push_back(compact_session_json(s));
                out["data"]["sessions"] = sessions;
            }
        }
    }

    if (full.contains("findings") && !json_empty_or_null(full["findings"])) out["findings"] = full["findings"];
    if (!ok && full.contains("error") && !json_empty_or_null(full["error"])) out["error"] = compact_error_json(full["error"]);
    if (full.contains("suggested_next_actions") && !json_empty_or_null(full["suggested_next_actions"])) {
        out["suggested_next_actions"] = full["suggested_next_actions"];
    }
    if (full.contains("warnings") && !json_empty_or_null(full["warnings"])) out["warnings"] = full["warnings"];
    if (full.contains("meta") && full["meta"].is_object() &&
        full["meta"].value("truncated", false)) {
        out["meta"] = {{"truncated", true}};
    }
    if (!ok && full.contains("session") && !json_empty_or_null(full["session"])) {
        out["session"] = compact_session_json(full["session"]);
    }
    return out;
}

Json finalize_response(const Json& req, const Json& full) {
    std::string verbosity = response_verbosity(req);
    if (verbosity == "full" || verbosity == "debug") return full;
    return compact_response(full);
}

void print_json(const Json& j) {
    printf("%s\n", j.dump(2).c_str());
}

bool get_string(const Json& obj, const char* key, std::string& out) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) return false;
    out = it->get<std::string>();
    return true;
}

std::string string_or(const Json& obj, const char* key, const std::string& def) {
    std::string v;
    return get_string(obj, key, v) ? v : def;
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

std::string create_session_quiet(SessionManager& manager, const std::string& fsdb, const std::string& name,
                                        const SessionTransportOptions& transport);

bool resolve_session(const Json& target,
                            bool allow_auto_open,
                            std::string& session_id,
                            SessionInfo& info,
                            std::string& error) {
    SessionManager manager;
    session_id.clear();
    auto sid_it = target.find("session_id");
    if (sid_it != target.end()) {
        if (!sid_it->is_string()) {
            error = "target.session_id must be a string";
            return false;
        }
        session_id = sid_it->get<std::string>();
        if (!manager.get_session(session_id, info)) {
            error = "session not found: " + session_id;
            return false;
        }
        if (!manager.ensure_session_current(session_id) || !manager.get_session(session_id, info)) {
            error = "session unavailable: " + session_id;
            return false;
        }
        return true;
    }

    std::string fsdb;
    if (get_string(target, "fsdb", fsdb)) {
        bool auto_open = bool_or(target, "auto_open", allow_auto_open);
        if (!auto_open) {
            error = "target.fsdb requires auto_open=true when session_id is omitted";
            return false;
        }
        std::string name;
        if (!get_string(target, "name", name)) {
            error = "target.name is required when auto-opening an FSDB";
            return false;
        }
        SessionTransportOptions transport;
        transport.transport = string_or(target, "transport", "uds");
        transport.bind_host = string_or(target, "bind_host", string_or(target, "bind", ""));
        transport.host = string_or(target, "host", "");
        transport.port = int_or(target, "port", 0);
        session_id = create_session_quiet(manager, fsdb, name, transport);
        if (session_id.empty() || !manager.get_session(session_id, info)) {
            error = "failed to open fsdb: " + fsdb;
            return false;
        }
        return true;
    }

    if (!manager.get_latest_session(info)) {
        error = "no active session";
        return false;
    }
    if (!manager.ensure_session_current(info.session_id) || !manager.get_session(info.session_id, info)) {
        error = "latest session unavailable";
        return false;
    }
    session_id = info.session_id;
    return true;
}

std::string create_session_quiet(SessionManager& manager, const std::string& fsdb, const std::string& name,
                                        const SessionTransportOptions& transport) {
    fflush(stdout);
    int saved_stdout = dup(STDOUT_FILENO);
    int saved_stderr = dup(STDERR_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    if (saved_stdout >= 0) fcntl(saved_stdout, F_SETFD, FD_CLOEXEC);
    if (saved_stderr >= 0) fcntl(saved_stderr, F_SETFD, FD_CLOEXEC);
    if (devnull >= 0) fcntl(devnull, F_SETFD, FD_CLOEXEC);
    if (saved_stdout >= 0 && devnull >= 0) {
        dup2(devnull, STDOUT_FILENO);
    }
    if (saved_stderr >= 0 && devnull >= 0) {
        dup2(devnull, STDERR_FILENO);
    }
    std::string sid = manager.create_session(fsdb, name, transport);
    fflush(stdout);
    fflush(stderr);
    if (saved_stdout >= 0) {
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
    }
    if (saved_stderr >= 0) {
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stderr);
    }
    if (devnull >= 0) close(devnull);
    return sid;
}

void fill_session(Json& out, const SessionInfo& info) {
    out["session"] = {
        {"id", info.session_id},
        {"fsdb", info.fsdb_file},
        {"pid", info.server_pid},
        {"transport", info.transport},
        {"socket_path", info.socket_path},
        {"host", info.host},
        {"port", info.port}
    };
}

bool capture_server_json(const std::string& session_id,
                                const std::string& cmd,
                                Json& data,
                                std::string& error) {
    std::string payload;
    if (!send_command_capture(session_id, cmd.c_str(), payload)) {
        error = trim(payload);
        if (error.compare(0, strlen(ERROR_PREFIX), ERROR_PREFIX) == 0) {
            error = trim(error.substr(strlen(ERROR_PREFIX)));
        }
        if (error.empty()) error = "server command failed";
        return false;
    }
    try {
        data = Json::parse(payload);
    } catch (const std::exception&) {
        data = trim(payload);
    }
    return true;
}

bool capture_server_text(const std::string& session_id,
                                const std::string& cmd,
                                std::string& payload,
                                std::string& error) {
    if (!send_command_capture(session_id, cmd.c_str(), payload)) {
        error = trim(payload);
        if (error.compare(0, strlen(ERROR_PREFIX), ERROR_PREFIX) == 0) {
            error = trim(error.substr(strlen(ERROR_PREFIX)));
        }
        if (error.empty()) error = "server command failed";
        return false;
    }
    payload = trim(payload);
    return true;
}

Json session_info_json(const SessionInfo& s) {
    Json j;
    j["id"] = s.session_id;
    j["pid"] = s.server_pid;
    j["transport"] = s.transport;
    j["socket_path"] = s.socket_path;
    j["host"] = s.host;
    j["bind_host"] = s.bind_host;
    j["port"] = s.port;
    j["server_host"] = s.server_host;
    j["fsdb"] = s.fsdb_file;
    j["created_at"] = static_cast<long long>(s.created_at);
    j["last_active"] = static_cast<long long>(s.last_active);
    j["fsdb_mtime"] = s.fsdb_mtime;
    j["fsdb_size"] = s.fsdb_size;
    j["fsdb_dev"] = s.fsdb_dev;
    j["fsdb_inode"] = s.fsdb_inode;
    return j;
}


} // namespace xdebug_waveform
