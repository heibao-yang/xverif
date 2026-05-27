#include "action_support.h"
#include "../apb/apb_manager.h"
#include "../axi/axi_manager.h"
#include "../event/event_manager.h"
#include "../list/list_manager.h"
#include "../protocol/protocol.h"
#include "../session/session_registry.h"
#include <algorithm>
#include <vector>

namespace xdebug_waveform {

int run_query(const Json& req, long long elapsed_ms) {
    std::string action;
    if (!get_string(req, "action", action)) {
        return print_error_and_return(req, "", "MISSING_FIELD", "request.action is required", elapsed_ms);
    }
    if (!action_known(action)) {
        return print_error_and_return(req, action, "UNKNOWN_ACTION", "action is not implemented: " + action, elapsed_ms);
    }
    Json target = req.value("target", Json::object());
    Json args = req.value("args", Json::object());
    Json limits = req.value("limits", Json::object());
    int max_rows = int_or(limits, "max_rows", int_or(limits, "max_events", 1000));
    bool verbosity_valid = true;
    response_verbosity(req, &verbosity_valid);
    if (!verbosity_valid) {
        return print_error_and_return(req, action, "INVALID_REQUEST", "output.verbosity must be compact, full, or debug", elapsed_ms);
    }

    auto ok_out = [&](const SessionInfo* info = nullptr) {
        Json out = base_response(req, action, true, elapsed_ms);
        if (info) fill_session(out, *info);
        return out;
    };
    auto emit = [&](const Json& out, int rc = 0) -> int {
        print_json(finalize_response(req, out));
        return rc;
    };

    if (action == "session.open") {
        std::string fsdb;
        if (!get_string(target, "fsdb", fsdb)) {
            return print_error_and_return(req, action, "MISSING_FIELD", "target.fsdb is required", elapsed_ms);
        }
        std::string name;
        if (!get_string(args, "name", name) && !get_string(target, "name", name)) {
            return print_error_and_return(req, action, "MISSING_FIELD", "session.open requires args.name", elapsed_ms);
        }
        if (!SessionRegistry::is_valid_session_name(name)) {
            return print_error_and_return(req, action, "INVALID_SESSION_ID", "invalid session name: " + name, elapsed_ms);
        }
        SessionManager manager;
        SessionRegistry registry;
        if (registry.exists(name)) {
            return print_error_and_return(req, action, "SESSION_ID_EXISTS", "session id already exists: " + name, elapsed_ms);
        }
        SessionTransportOptions transport;
        transport.transport = string_or(args, "transport", string_or(target, "transport", "uds"));
        transport.bind_host = string_or(args, "bind_host", string_or(args, "bind", string_or(target, "bind_host", string_or(target, "bind", ""))));
        transport.host = string_or(args, "host", string_or(target, "host", ""));
        transport.port = int_or(args, "port", int_or(target, "port", 0));
        std::string sid = create_session_quiet(manager, fsdb, name, transport);
        SessionInfo info;
        if (sid.empty() || !manager.get_session(sid, info)) {
            return print_error_and_return(req, action, "INVALID_TARGET", "failed to open fsdb: " + fsdb, elapsed_ms);
        }
        Json out = ok_out(&info);
        out["summary"] = {{"session_id", sid}, {"fsdb", info.fsdb_file}};
        out["data"]["session"] = session_info_json(info);
        return emit(out);
    }

    if (action == "session.list") {
        SessionManager manager;
        Json out = ok_out();
        Json arr = Json::array();
        for (const auto& s : manager.list_sessions()) arr.push_back(session_info_json(s));
        out["summary"] = {{"session_count", arr.size()}};
        out["data"]["sessions"] = arr;
        return emit(out);
    }

    if (action == "session.gc") {
        SessionManager manager;
        manager.gc_sessions();
        Json out = ok_out();
        out["summary"] = {{"status", "completed"}};
        return emit(out);
    }

    if (action == "session.kill") {
        SessionManager manager;
        bool ok = false;
        if (string_or(args, "id", "") == "all" || string_or(args, "session_id", "") == "all") {
            ok = manager.kill_all_sessions();
        } else {
            std::string sid = string_or(target, "session_id", string_or(args, "session_id", string_or(args, "id", "")));
            if (sid.empty()) return print_error_and_return(req, action, "MISSING_FIELD", "session.kill requires target.session_id or args.id", elapsed_ms);
            ok = manager.kill_session(sid);
        }
        if (!ok) return print_error_and_return(req, action, "SESSION_UNHEALTHY", "failed to kill session", elapsed_ms);
        Json out = ok_out();
        out["summary"] = {{"status", "removed"}};
        return emit(out);
    }

    if (action == "session.doctor") {
        std::string sid = string_or(target, "session_id", string_or(args, "session_id", ""));
        if (sid.empty()) return print_error_and_return(req, action, "MISSING_FIELD", "session.doctor requires session_id", elapsed_ms);
        SessionManager manager;
        SessionHealth h = manager.diagnose_session(sid);
        Json out = base_response(req, action, h.healthy, elapsed_ms);
        fill_session(out, h.info);
        out["summary"] = {{"healthy", h.healthy}, {"status", session_health_status_name(h.status)}, {"message", h.message}};
        out["data"]["health"] = out["summary"];
        if (!h.healthy) {
            out["error"] = {{"code", "SESSION_UNHEALTHY"}, {"message", h.message}, {"recoverable", true}, {"candidates", Json::array()}, {"suggested_actions", Json::array()}};
        }
        return emit(out, h.healthy ? 0 : 1);
    }

    std::string sid;
    SessionInfo info;
    std::string err;
    if (!resolve_session(target, true, sid, info, err)) {
        return print_error_and_return(req, action, "SESSION_NOT_FOUND", err, elapsed_ms);
    }

    if (server_ai_action(action)) {
        Json data;
        std::string cmd = std::string(CMD_AI_QUERY) + " " + req.dump();
        if (!capture_server_json(sid, cmd, data, err)) {
            std::string code = err.find("Signal not found") != std::string::npos ? "SIGNAL_NOT_FOUND" :
                               err.find("Clock signal not found") != std::string::npos ? "SIGNAL_NOT_FOUND" :
                               err.find("SIGNAL_NOT_FOUND") != std::string::npos ? "SIGNAL_NOT_FOUND" :
                               err.find("CURSOR_NOT_FOUND") != std::string::npos ? "CURSOR_NOT_FOUND" :
                               err.find("TIME_OUT_OF_RANGE") != std::string::npos ? "TIME_OUT_OF_RANGE" :
                               err.find("CLOCK_OFFSET_UNSUPPORTED") != std::string::npos ? "CLOCK_OFFSET_UNSUPPORTED" :
                               err.find("TIME_SPEC_INVALID") != std::string::npos ? "TIME_SPEC_INVALID" :
                               err.find("Invalid time") != std::string::npos ? "TIME_SPEC_INVALID" :
                               err.find("Invalid TimeSpec") != std::string::npos ? "TIME_SPEC_INVALID" :
                               err.find("config not found") != std::string::npos ? "INVALID_REQUEST" :
                               err.find("expression") != std::string::npos ? "EXPR_PARSE_FAILED" :
                               "WAVE_QUERY_FAILED";
            return print_error_and_return(req, action, code, err, elapsed_ms);
        }
        Json out = ok_out(&info);
        out["data"] = data;
        std::string begin_spec, end_spec;
        bool around_window = false;
        if (build_range_specs(args, begin_spec, end_spec, around_window, err) &&
            (args.contains("time_range") || args.contains("begin") || args.contains("end") || args.contains("around"))) {
            fill_resolved_range(out, sid, begin_spec, end_spec, around_window, err);
        }
        std::string at_spec = string_or(args, "at", string_or(args, "time", ""));
        if (!at_spec.empty() && out["data"].is_object() && !out["data"].contains("resolved_time")) {
            Json resolved = resolve_time_spec_json(sid, at_spec, false, err);
            if (!resolved.is_null()) out["data"]["resolved_time"] = resolved;
        }
        if (data.contains("truncated")) out["meta"]["truncated"] = data["truncated"];
        if (data.contains("findings")) out["findings"] = data["findings"];
        if (action == "sampled_pulse.inspect") {
            out["summary"] = {{"sample_count", data.value("sample_count", 0)},
                              {"sampled_high_cycles", data.value("sampled_high_cycles", 0)},
                              {"raw_valid_transition_count", data.value("raw_valid_transition_count", 0)},
                              {"payload_transition_count", data.value("payload_transition_count", 0)},
                              {"risk_count", data.value("risk_count", 0)}};
        } else if (action == "window.verify") {
            out["summary"] = {{"all_passed", data.value("all_passed", false)},
                              {"sample_count", data.value("sample_count", 0)},
                              {"failed_samples", data.value("failed_samples", 0)},
                              {"unknown_samples", data.value("unknown_samples", 0)}};
        } else if (action == "handshake.inspect") {
            out["summary"] = {{"transfer_count", data.value("transfer_count", 0)},
                              {"max_stall_cycles", data.value("max_stall_cycles", 0)}};
        } else if (action == "detect_anomaly") {
            out["summary"] = {{"finding_count", data.value("finding_count", 0)}};
        } else if (data.contains("transaction_count")) {
            out["summary"] = {{"transaction_count", data["transaction_count"]}};
        } else if (data.contains("sample_count")) {
            out["summary"] = {{"sample_count", data["sample_count"]}};
        } else if (data.contains("transition_count")) {
            out["summary"] = {{"transition_count", data["transition_count"]}};
        } else if (data.contains("status")) {
            out["summary"] = {{"status", data["status"]}, {"known", data.value("known", false)}};
        }
        return emit(out);
    }

    if (action == "value.at") {
        std::string signal, time;
        if (!get_string(args, "signal", signal)) {
            return print_error_and_return(req, action, "MISSING_FIELD", "value.at requires args.signal", elapsed_ms);
        }
        if (!get_string(args, "at", time) && !get_string(args, "time", time)) {
            return print_error_and_return(req, action, "MISSING_FIELD", "value.at requires args.time or args.at", elapsed_ms);
        }
        std::string raw;
        if (!query_value(sid, signal, time, fmt_char(args), raw, err)) {
            bool not_found = err.find("Signal not found") != std::string::npos ||
                             err.find("Failed to read value for signal") != std::string::npos ||
                             err.find("SIGNAL_NOT_FOUND") != std::string::npos;
            std::string code = not_found ? "SIGNAL_NOT_FOUND" :
                               err.find("CURSOR_NOT_FOUND") != std::string::npos ? "CURSOR_NOT_FOUND" :
                               err.find("TIME_OUT_OF_RANGE") != std::string::npos ? "TIME_OUT_OF_RANGE" :
                               err.find("CLOCK_OFFSET_UNSUPPORTED") != std::string::npos ? "CLOCK_OFFSET_UNSUPPORTED" :
                               err.find("TIME_SPEC_INVALID") != std::string::npos ? "TIME_SPEC_INVALID" :
                               err.find("Invalid") != std::string::npos ? "TIME_SPEC_INVALID" :
                               "WAVE_QUERY_FAILED";
            return print_error_and_return(req, action, code, err, elapsed_ms);
        }
        Json out = ok_out(&info);
        out["summary"] = {{"signal", signal}, {"time", time}, {"known", !contains_xz(raw)}};
        out["data"]["signal"] = signal;
        out["data"]["time"] = time;
        Json resolved = resolve_time_spec_json(sid, time, false, err);
        if (!resolved.is_null()) out["data"]["resolved_time"] = resolved;
        out["data"]["value"] = make_value_object(raw);
        return emit(out);
    }

    if (action == "value.batch_at") {
        std::string time;
        if ((!get_string(args, "at", time) && !get_string(args, "time", time)) ||
            !args.contains("signals") || !args["signals"].is_array()) {
            return print_error_and_return(req, action, "MISSING_FIELD", "value.batch_at requires args.time/args.at and args.signals[]", elapsed_ms);
        }
        Json arr = Json::array();
        int unknown = 0, missing = 0;
        for (const auto& s : args["signals"]) {
            if (!s.is_string()) continue;
            std::string signal = s.get<std::string>();
            std::string raw;
            Json item;
            item["signal"] = signal;
            item["time"] = time;
            if (query_value(sid, signal, time, fmt_char(args), raw, err)) {
                item["status"] = "ok";
                item["value"] = make_value_object(raw);
                if (contains_xz(raw)) unknown++;
            } else {
                item["status"] = "not_found";
                item["value"] = nullptr;
                item["error"] = err;
                missing++;
            }
            arr.push_back(item);
        }
        Json out = ok_out(&info);
        out["summary"] = {{"time", time}, {"signal_count", arr.size()}, {"unknown_count", unknown}, {"missing_count", missing}};
        Json resolved = resolve_time_spec_json(sid, time, false, err);
        if (!resolved.is_null()) out["data"]["resolved_time"] = resolved;
        out["data"]["values"] = arr;
        return emit(out, missing == 0 ? 0 : 1);
    }

    if (action == "scope.list") {
        std::string path;
        if (!get_string(args, "path", path)) return print_error_and_return(req, action, "MISSING_FIELD", "scope.list requires args.path", elapsed_ms);
        bool recursive = bool_or(args, "recursive", false);
        Json data;
        std::string cmd = std::string(CMD_SCOPE) + " " + path + " " + (recursive ? "1" : "0") + " json";
        if (!capture_server_json(sid, cmd, data, err)) return print_error_and_return(req, action, "WAVE_QUERY_FAILED", err, elapsed_ms);
        bool truncated = false;
        if (data.is_array() && max_rows >= 0 && data.size() > static_cast<size_t>(max_rows)) {
            Json limited = Json::array();
            for (int i = 0; i < max_rows; ++i) limited.push_back(data[i]);
            data = limited;
            truncated = true;
        }
        Json out = ok_out(&info);
        out["summary"] = {{"path", path}, {"recursive", recursive}, {"signal_count", data.is_array() ? data.size() : 0}, {"truncated", truncated}};
        out["data"]["signals"] = data;
        out["meta"]["truncated"] = truncated;
        return emit(out);
    }

    if (action.compare(0, 5, "list.") == 0) {
        ListManager lm;
        std::string name = string_or(args, "name", string_or(args, "list", ""));
        if (name.empty() && action != "list.create") lm.get_latest_list(sid, name);
        if (action == "list.create") {
            if (name.empty()) return print_error_and_return(req, action, "MISSING_FIELD", "list.create requires args.name", elapsed_ms);
            if (!lm.create_list(sid, name)) return print_error_and_return(req, action, "INTERNAL_ERROR", "failed to create list", elapsed_ms);
            Json out = ok_out(&info); out["summary"] = {{"name", name}, {"status", "created"}}; return emit(out);
        }
        if (name.empty()) return print_error_and_return(req, action, "MISSING_FIELD", "list action requires args.name or latest list", elapsed_ms);
        if (action == "list.add") {
            std::string signal;
            if (!get_string(args, "signal", signal)) return print_error_and_return(req, action, "MISSING_FIELD", "list.add requires args.signal", elapsed_ms);
            std::string payload;
            if (!capture_server_text(sid, std::string(CMD_SIGNAL_CHECK) + " " + signal, payload, err)) {
                return print_error_and_return(req, action, "SIGNAL_NOT_FOUND", err, elapsed_ms);
            }
            if (!lm.add_signal(sid, name, signal)) return print_error_and_return(req, action, "INTERNAL_ERROR", "failed to add signal", elapsed_ms);
            Json out = ok_out(&info); out["summary"] = {{"name", name}, {"signal", signal}, {"status", "added"}}; return emit(out);
        }
        if (action == "list.delete") {
            std::string signal = string_or(args, "signal", string_or(args, "index", ""));
            if (signal.empty()) return print_error_and_return(req, action, "MISSING_FIELD", "list.delete requires args.signal or args.index", elapsed_ms);
            if (!lm.del_signal(sid, name, signal)) return print_error_and_return(req, action, "INTERNAL_ERROR", "failed to delete signal", elapsed_ms);
            Json out = ok_out(&info); out["summary"] = {{"name", name}, {"removed", signal}}; return emit(out);
        }
        if (action == "list.show") {
            SignalList list;
            if (!lm.get_list(sid, name, list)) return print_error_and_return(req, action, "INVALID_REQUEST", "list not found", elapsed_ms);
            Json arr = Json::array();
            for (size_t i = 0; i < list.signals.size(); ++i) arr.push_back({{"index", i + 1}, {"signal", list.signals[i]}});
            Json out = ok_out(&info); out["summary"] = {{"name", name}, {"signal_count", arr.size()}}; out["data"]["signals"] = arr; return emit(out);
        }
        if (action == "list.value_at") {
            std::string time;
            if (!get_string(args, "at", time) && !get_string(args, "time", time)) {
                return print_error_and_return(req, action, "MISSING_FIELD", "list.value_at requires args.time or args.at", elapsed_ms);
            }
            Json data;
            std::string cmd = std::string(CMD_LIST_VALUE) + " " + name + " " + time + " " + fmt_char(args) + " json";
            bool ok = capture_server_json(sid, cmd, data, err);
            Json out = base_response(req, action, ok, elapsed_ms);
            fill_session(out, info);
            out["summary"] = {{"name", name}, {"time", time}};
            if (data.is_object() && data.contains("values") && data["values"].is_object()) {
                data["values"] = make_value_map(data["values"]);
            } else if (data.is_object()) {
                data = make_value_map(data);
            }
            out["data"] = data;
            Json resolved = resolve_time_spec_json(sid, time, false, err);
            if (!resolved.is_null()) out["data"]["resolved_time"] = resolved;
            if (!ok) out["error"] = {{"code", "SIGNAL_NOT_FOUND"}, {"message", err}, {"recoverable", true}, {"candidates", Json::array()}, {"suggested_actions", Json::array()}};
            return emit(out, ok ? 0 : 1);
        }
        if (action == "list.validate") {
            Json data;
            bool ok = capture_server_json(sid, std::string(CMD_LIST_VALIDATE) + " " + name + " json", data, err);
            Json out = base_response(req, action, ok, elapsed_ms);
            fill_session(out, info);
            out["summary"] = {{"name", name}, {"all_found", ok}};
            out["data"]["signals"] = data;
            if (!ok) out["error"] = {{"code", "SIGNAL_NOT_FOUND"}, {"message", err}, {"recoverable", true}, {"candidates", Json::array()}, {"suggested_actions", Json::array()}};
            return emit(out, ok ? 0 : 1);
        }
        if (action == "list.diff") {
            std::string begin, end;
            bool around_window = false;
            if (!build_range_specs(args, begin, end, around_window, err)) {
                return print_error_and_return(req, action, "TIME_SPEC_INVALID", err, elapsed_ms);
            }
            std::string payload;
            if (!capture_server_text(sid, std::string(CMD_LIST_DIFF) + " " + name + " " + begin + " " + end, payload, err)) {
                return print_error_and_return(req, action, "WAVE_QUERY_FAILED", err, elapsed_ms);
            }
            Json out = ok_out(&info);
            out["summary"] = {{"name", name}, {"diff_time", payload}};
            out["data"]["time"] = payload;
            fill_resolved_range(out, sid, begin, end, around_window, err);
            return emit(out);
        }
    }

    if (action.compare(0, 4, "apb.") == 0) {
        ApbManager am;
        std::string name = string_or(args, "name", "");
        if (action == "apb.config.load") {
            if (name.empty()) return print_error_and_return(req, action, "MISSING_FIELD", "apb.config.load requires args.name", elapsed_ms);
            Json cfg_json; if (!load_config_json_arg(args, cfg_json, err)) return print_error_and_return(req, action, "INVALID_REQUEST", err, elapsed_ms);
            ApbConfig cfg; if (!parse_apb_config(cfg_json, cfg, err)) return print_error_and_return(req, action, "INVALID_REQUEST", err, elapsed_ms);
            cfg.name = name;
            if (!am.create_apb(sid, cfg)) return print_error_and_return(req, action, "INTERNAL_ERROR", "failed to save APB config", elapsed_ms);
            Json out = ok_out(&info); out["summary"] = {{"name", name}, {"status", "loaded"}}; out["data"]["config"] = apb_config_json(cfg); return emit(out);
        }
        if (name.empty()) am.get_latest_apb(sid, name);
        if (name.empty()) return print_error_and_return(req, action, "MISSING_FIELD", "APB action requires args.name or latest config", elapsed_ms);
        if (action == "apb.config.list") {
            ApbConfig cfg; if (!am.get_apb(sid, name, cfg)) return print_error_and_return(req, action, "INVALID_REQUEST", "APB config not found", elapsed_ms);
            Json out = ok_out(&info); out["summary"] = {{"name", name}}; out["data"]["config"] = apb_config_json(cfg); return emit(out);
        }
        std::string cmd;
        if (action == "apb.query") {
            std::string dir = string_or(args, "direction", "wr");
            cmd = std::string(dir == "rd" ? CMD_APB_RD : CMD_APB_WR) + " " + name;
            if (args.contains("address")) cmd += " addr " + arg_text(args["address"]);
            if (args.contains("num")) cmd += " num " + std::to_string(args["num"].get<int>());
            if (bool_or(args, "last", false)) cmd += " last";
            cmd += " json";
        } else {
            std::string op = string_or(args, "op", "begin");
            const char* pcmd = op == "next" ? CMD_APB_NEXT : op == "pre" ? CMD_APB_PREV : op == "last" ? CMD_APB_LAST : CMD_APB_BEGIN;
            cmd = std::string(pcmd) + " " + name + " " + string_or(args, "direction", "all") + " json";
        }
        Json data; if (!capture_server_json(sid, cmd, data, err)) return print_error_and_return(req, action, "WAVE_QUERY_FAILED", err, elapsed_ms);
        Json out = ok_out(&info); out["summary"] = {{"name", name}}; out["data"] = data; return emit(out);
    }

    if (action.compare(0, 4, "axi.") == 0) {
        AxiManager am;
        std::string name = string_or(args, "name", "");
        if (action == "axi.config.load") {
            if (name.empty()) return print_error_and_return(req, action, "MISSING_FIELD", "axi.config.load requires args.name", elapsed_ms);
            Json cfg_json; if (!load_config_json_arg(args, cfg_json, err)) return print_error_and_return(req, action, "INVALID_REQUEST", err, elapsed_ms);
            AxiConfig cfg; if (!parse_axi_config(cfg_json, cfg, err)) return print_error_and_return(req, action, "INVALID_REQUEST", err, elapsed_ms);
            cfg.name = name;
            if (!am.create_axi(sid, cfg)) return print_error_and_return(req, action, "INTERNAL_ERROR", "failed to save AXI config", elapsed_ms);
            Json out = ok_out(&info); out["summary"] = {{"name", name}, {"status", "loaded"}}; out["data"]["config"] = axi_config_json(cfg); return emit(out);
        }
        if (name.empty()) am.get_latest_axi(sid, name);
        if (name.empty()) return print_error_and_return(req, action, "MISSING_FIELD", "AXI action requires args.name or latest config", elapsed_ms);
        if (action == "axi.config.list") {
            AxiConfig cfg; if (!am.get_axi(sid, name, cfg)) return print_error_and_return(req, action, "INVALID_REQUEST", "AXI config not found", elapsed_ms);
            Json out = ok_out(&info); out["summary"] = {{"name", name}}; out["data"]["config"] = axi_config_json(cfg); return emit(out);
        }
        std::string cmd;
        if (action == "axi.query") {
            std::string dir = string_or(args, "direction", "wr");
            cmd = std::string(dir == "rd" ? CMD_AXI_RD : CMD_AXI_WR) + " " + name;
            if (args.contains("address")) cmd += " addr " + arg_text(args["address"]);
            if (args.contains("id")) cmd += " id " + arg_text(args["id"]);
            if (args.contains("num")) cmd += " num " + std::to_string(args["num"].get<int>());
            if (bool_or(args, "last", false)) cmd += " last";
            cmd += " json";
        } else if (action == "axi.analysis") {
            std::string analysis = string_or(args, "analysis", "latency");
            cmd = std::string(analysis == "osd" ? CMD_AXI_OSD : CMD_AXI_LATENCY) + " " + name + " " + string_or(args, "direction", "all");
            if (args.contains("id")) cmd += " id " + arg_text(args["id"]);
            cmd += " json";
        } else {
            std::string op = string_or(args, "op", "begin");
            const char* pcmd = op == "next" ? CMD_AXI_NEXT : op == "pre" ? CMD_AXI_PREV : op == "last" ? CMD_AXI_LAST : CMD_AXI_BEGIN;
            cmd = std::string(pcmd) + " " + name + " " + string_or(args, "direction", "all") + " json";
        }
        Json data; if (!capture_server_json(sid, cmd, data, err)) return print_error_and_return(req, action, "WAVE_QUERY_FAILED", err, elapsed_ms);
        Json out = ok_out(&info); out["summary"] = {{"name", name}}; out["data"] = data; return emit(out);
    }

    if (action.compare(0, 6, "event.") == 0) {
        EventManager em;
        std::string name = string_or(args, "name", "");
        if (action == "event.config.load") {
            if (name.empty()) return print_error_and_return(req, action, "MISSING_FIELD", "event.config.load requires args.name", elapsed_ms);
            Json cfg_json; if (!load_config_json_arg(args, cfg_json, err)) return print_error_and_return(req, action, "INVALID_REQUEST", err, elapsed_ms);
            EventConfig cfg; if (!parse_event_config(cfg_json, cfg, err)) return print_error_and_return(req, action, "INVALID_REQUEST", err, elapsed_ms);
            cfg.name = name;
            if (!em.create_event(sid, info.fsdb_file, cfg)) return print_error_and_return(req, action, "INTERNAL_ERROR", "failed to save event config", elapsed_ms);
            Json out = ok_out(&info); out["summary"] = {{"name", name}, {"status", "loaded"}}; out["data"]["config"] = event_config_json(cfg); return emit(out);
        }
        if (action == "event.config.list") {
            Json out = ok_out(&info);
            if (name.empty()) {
                Json arr = em.list_events(sid, info.fsdb_file);
                out["summary"] = {{"count", arr.size()}};
                out["data"]["events"] = arr;
            } else {
                EventConfig cfg; if (!em.get_event(sid, info.fsdb_file, name, cfg)) return print_error_and_return(req, action, "INVALID_REQUEST", "event config not found", elapsed_ms);
                out["summary"] = {{"name", name}};
                out["data"]["config"] = event_config_json(cfg);
            }
            return emit(out);
        }
        if (name.empty()) em.get_latest_event(sid, info.fsdb_file, name);
        if (name.empty()) return print_error_and_return(req, action, "MISSING_FIELD", "event action requires args.name or latest config", elapsed_ms);
        std::string expr; if (!get_string(args, "expr", expr)) return print_error_and_return(req, action, "MISSING_FIELD", "event.find/export requires args.expr", elapsed_ms);
        expr = compact_expr_ws(expr);
        std::string begin, end;
        bool around_window = false;
        if (!build_range_specs(args, begin, end, around_window, err)) {
            return print_error_and_return(req, action, "TIME_SPEC_INVALID", err, elapsed_ms);
        }
        int limit = action == "event.find" ? 1 : int_or(limits, "max_rows", int_or(args, "limit", 1000));
        Json ctx = args.value("context", Json::object());
        std::string mode = "json";
        std::string cmd;
        if (ctx.is_object() && ctx.contains("window")) {
            std::string window = string_or(ctx, "window", "0ns");
            std::string axi = string_or(ctx, "axi", "-"); if (axi.empty()) axi = "-";
            std::string apb = string_or(ctx, "apb", "-"); if (apb.empty()) apb = "-";
            cmd = std::string(action == "event.find" ? CMD_EVENT_FIND_CTX : CMD_EVENT_EXPORT_CTX) + " " + name + " " + begin + " " + end + " " + std::to_string(limit) + " " + mode + " " + window + " " + axi + " " + apb + " expr " + expr;
        } else {
            cmd = std::string(action == "event.find" ? CMD_EVENT_FIND : CMD_EVENT_EXPORT) + " " + name + " " + begin + " " + end + " " + std::to_string(limit) + " " + mode + " expr " + expr;
        }
        Json data; if (!capture_server_json(sid, cmd, data, err)) return print_error_and_return(req, action, "WAVE_QUERY_FAILED", err, elapsed_ms);
        Json raw_events = data;
        Json aggregate = Json::object();
        bool has_aggregate = action == "event.export" && args.contains("aggregate") && args["aggregate"].is_object();
        bool include_events = true;
        if (has_aggregate) {
            aggregate = aggregate_events(raw_events, args["aggregate"], limit);
            include_events = args["aggregate"].value("events", true);
        }
        Json out = ok_out(&info);
        out["summary"] = {{"name", name}, {"begin", begin}, {"end", end}};
        if (data.is_array()) out["summary"]["event_count"] = data.size();
        if (has_aggregate) {
            out["summary"]["aggregate_count"] = aggregate.value("count", 0);
            out["summary"]["limited"] = aggregate.value("limited", false);
            if (aggregate.contains("group_count")) out["summary"]["group_count"] = aggregate["group_count"];
            out["data"]["aggregate"] = aggregate;
        }
        if (include_events) out["data"]["events"] = simplify_event_value_objects(data);
        fill_resolved_range(out, sid, begin, end, around_window, err);
        return emit(out);
    }

    if (action == "verify.conditions") {
        std::string time;
        if ((!get_string(args, "at", time) && !get_string(args, "time", time)) ||
            !args.contains("conditions") || !args["conditions"].is_array()) {
            return print_error_and_return(req, action, "MISSING_FIELD", "verify.conditions requires args.time/args.at and args.conditions[]", elapsed_ms);
        }
        Json checks = Json::array();
        int passed = 0, failed = 0, unknown = 0;
        for (const auto& cond : args["conditions"]) {
            std::string signal, op, expected;
            get_string(cond, "signal", signal);
            get_string(cond, "op", op);
            get_string(cond, "value", expected);
            Json item = {{"signal", signal}, {"time", time}, {"op", op}, {"expected", expected}};
            std::string raw;
            if (!query_value(sid, signal, time, 'H', raw, err)) {
                item["status"] = "unknown"; item["known"] = false; item["pass"] = nullptr; item["error"] = err; unknown++;
            } else if (contains_xz(raw) || contains_xz(expected)) {
                item["observed"] = make_value_object(raw); item["status"] = "unknown"; item["known"] = false; item["pass"] = nullptr; unknown++;
            } else {
                bool eq = normalize_numeric(raw) == normalize_numeric(expected);
                bool pass = (op == "!=") ? !eq : eq;
                item["observed"] = make_value_object(raw); item["status"] = pass ? "pass" : "fail"; item["known"] = true; item["pass"] = pass;
                if (pass) passed++; else failed++;
            }
            checks.push_back(item);
        }
        Json out = ok_out(&info);
        out["summary"] = {{"all_passed", failed == 0 && unknown == 0}, {"passed", passed}, {"failed", failed}, {"unknown", unknown}};
        Json resolved = resolve_time_spec_json(sid, time, false, err);
        if (!resolved.is_null()) out["data"]["resolved_time"] = resolved;
        out["data"]["checks"] = checks;
        return emit(out);
    }

    if (action == "expr.eval_at") {
        std::string time, expr;
        if ((!get_string(args, "at", time) && !get_string(args, "time", time)) ||
            !get_string(args, "expr", expr) || !args.contains("signals") || !args["signals"].is_object()) {
            return print_error_and_return(req, action, "MISSING_FIELD", "expr.eval_at requires args.time/args.at, args.expr, args.signals", elapsed_ms);
        }
        Json values = Json::object();
        Json operands = Json::array();
        int unknown = 0;
        for (auto it = args["signals"].begin(); it != args["signals"].end(); ++it) {
            std::string alias = it.key();
            std::string signal = it.value().get<std::string>();
            std::string raw;
            Json item = {{"alias", alias}, {"signal", signal}};
            if (query_value(sid, signal, time, 'H', raw, err)) {
                item["value"] = make_value_object(raw);
                if (contains_xz(raw)) unknown++;
            } else {
                item["value"] = nullptr;
                item["error"] = err;
                unknown++;
            }
            values[alias] = item;
            operands.push_back(item);
        }
        bool expression_ok = false;
        Tri v = evaluate_expression(expr, values, expression_ok);
        if (!expression_ok) return print_error_and_return(req, action, "EXPR_PARSE_FAILED", "failed to parse expression", elapsed_ms);
        Json out = ok_out(&info);
        out["summary"] = {{"expr", expr}, {"expr_value", v == Tri::True ? Json(true) : v == Tri::False ? Json(false) : Json(nullptr)}, {"status", tri_text(v)}, {"known", v != Tri::Unknown}};
        Json resolved = resolve_time_spec_json(sid, time, false, err);
        if (!resolved.is_null()) out["data"]["resolved_time"] = resolved;
        out["data"]["operands"] = operands;
        out["data"]["unknown_count"] = unknown;
        return emit(out);
    }

    return print_error_and_return(req, action, "UNKNOWN_ACTION", "unhandled action: " + action, elapsed_ms);
}


} // namespace xdebug_waveform
