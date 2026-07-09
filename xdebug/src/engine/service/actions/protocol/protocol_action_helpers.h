#pragma once

#include "service/engine_action_handler.h"

namespace xdebug_design {

inline Json protocol_example_args(const std::string& action) {
    if (action == "axi.config.load") {
        return Json{{"name", "axi0"},
                    {"config", {{"clock", "top.u.clk"},
                                {"rst_n", "top.u.rst_n"},
                                {"awvalid", "top.u.awvalid"},
                                {"awready", "top.u.awready"},
                                {"awaddr", "top.u.awaddr"},
                                {"awid", "top.u.awid"},
                                {"awlen", "top.u.awlen"},
                                {"awsize", "top.u.awsize"},
                                {"awburst", "top.u.awburst"},
                                {"wvalid", "top.u.wvalid"},
                                {"wready", "top.u.wready"},
                                {"wdata", "top.u.wdata"},
                                {"wstrb", "top.u.wstrb"},
                                {"wlast", "top.u.wlast"},
                                {"bvalid", "top.u.bvalid"},
                                {"bready", "top.u.bready"},
                                {"bid", "top.u.bid"},
                                {"bresp", "top.u.bresp"},
                                {"arvalid", "top.u.arvalid"},
                                {"arready", "top.u.arready"},
                                {"araddr", "top.u.araddr"},
                                {"arid", "top.u.arid"},
                                {"arlen", "top.u.arlen"},
                                {"arsize", "top.u.arsize"},
                                {"arburst", "top.u.arburst"},
                                {"rvalid", "top.u.rvalid"},
                                {"rready", "top.u.rready"},
                                {"rdata", "top.u.rdata"},
                                {"rid", "top.u.rid"},
                                {"rresp", "top.u.rresp"},
                                {"rlast", "top.u.rlast"}}}};
    }
    if (action == "apb.config.load") {
        return Json{{"name", "apb0"},
                    {"config", {{"clock", "top.u.clk"},
                                {"rst_n", "top.u.rst_n"},
                                {"paddr", "top.u.paddr"},
                                {"psel", "top.u.psel"},
                                {"penable", "top.u.penable"},
                                {"pwrite", "top.u.pwrite"},
                                {"pwdata", "top.u.pwdata"},
                                {"prdata", "top.u.prdata"}}}};
    }
    if (action == "axi.export") {
        return Json{{"name", "axi0"},
                    {"time_range", {{"begin", "0ns"}, {"end", "1000ns"}}},
                    {"output", {{"path", "/tmp/xdebug-axi-export"}, {"file_format", "tsv"}}}};
    }
    if (action == "axi.cursor" || action == "apb.cursor") {
        return Json{{"name", action.rfind("axi.", 0) == 0 ? "axi0" : "apb0"},
                    {"op", "begin"},
                    {"direction", "all"}};
    }
    if (action == "axi.query" || action == "apb.query") {
        return Json{{"name", action.rfind("axi.", 0) == 0 ? "axi0" : "apb0"},
                    {"direction", "write"},
                    {"query", {{"line_limit", 8}}}};
    }
    if (action == "axi.analysis") {
        return Json{{"name", "axi0"}, {"analysis", "latency"}, {"direction", "all"}};
    }
    return Json{{"name", action.rfind("axi.", 0) == 0 ? "axi0" : "apb0"}};
}

inline Json protocol_action_example(const std::string& action) {
    return Json{{"api_version", "xdebug.v1"},
                {"action", action},
                {"target", {{"session_id", "case_a"}}},
                {"args", protocol_example_args(action)}};
}

inline Json protocol_missing_name_error(const std::string& action,
                                        const std::string& protocol) {
    return make_handler_error(
        "MISSING_FIELD",
        "args.name is required",
        {{"invalid_arg", "args.name"},
         {"expected", "name of a loaded " + protocol + " config"},
         {"correct_example", protocol_action_example(action)},
         {"example_note", "Example only; load a config first or use an existing config name from " + protocol + ".config.list."},
         {"next_actions", Json::array({"Call " + protocol + ".config.list to inspect loaded configs.",
                                        "Call " + protocol + ".config.load before query/cursor/analysis/export."})}});
}

inline Json protocol_config_not_found_error(const std::string& action,
                                            const std::string& protocol,
                                            const std::string& name) {
    return make_handler_error(
        "CONFIG_NOT_FOUND",
        protocol + " config not found: " + name,
        {{"invalid_arg", "args.name"},
         {"expected", "name of a previously loaded " + protocol + " config"},
         {"missing_name", name},
         {"missing_resource", protocol + " config"},
         {"correct_example", protocol_action_example(action)},
         {"example_note", "Example only; choose an existing config name or load this config before using it."},
         {"next_actions", Json::array({"Call " + protocol + ".config.list to inspect loaded configs.",
                                        "Call " + protocol + ".config.load before this action."})}});
}

inline Json protocol_invalid_arg_error(const std::string& action,
                                       const std::string& invalid_arg,
                                       const std::string& message,
                                       const std::string& expected,
                                       const Json& extra = Json::object()) {
    Json details = {{"invalid_arg", invalid_arg},
                    {"expected", expected},
                    {"correct_example", protocol_action_example(action)},
                    {"example_note", "Example only; replace placeholders with active signal paths, names, and time values."}};
    for (auto it = extra.begin(); it != extra.end(); ++it) details[it.key()] = it.value();
    return make_handler_error("INVALID_ARGUMENT", message, details);
}

inline Json protocol_invalid_enum_error(const std::string& action,
                                        const std::string& invalid_arg,
                                        const std::string& message,
                                        const Json& allowed_values) {
    return make_handler_error(
        "INVALID_ENUM",
        message,
        {{"invalid_arg", invalid_arg},
         {"expected", "one of allowed_values"},
         {"allowed_values", allowed_values},
         {"correct_example", protocol_action_example(action)},
         {"example_note", "Example only; choose a value from allowed_values."}});
}

inline Json protocol_time_error(const std::string& action,
                                const std::string& invalid_arg,
                                const std::string& message) {
    return make_handler_error(
        "INVALID_TIME",
        message,
        {{"invalid_arg", invalid_arg},
         {"expected", "time strings in args.time_range.begin/end such as 0ns and 100ns"},
         {"correct_example", protocol_action_example(action)}});
}

inline Json protocol_analyze_error(const std::string& action,
                                   const std::string& protocol,
                                   const std::string& name,
                                   const std::string& message) {
    return make_handler_error(
        "ACTION_FAILED",
        message,
        {{"cause_code", "ANALYZE_FAILED"},
         {"invalid_arg", "args.name"},
         {"expected", "an analyzable " + protocol + " config in the active waveform session"},
         {"missing_name", name},
         {"correct_example", protocol_action_example(action)},
         {"next_actions", Json::array({"Confirm the config name with " + protocol + ".config.list.",
                                        "Validate signal paths in the loaded config against the active FSDB."})}});
}

} // namespace xdebug_design
