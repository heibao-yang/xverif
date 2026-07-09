#pragma once

#include "service/engine_action_handler.h"

namespace xdebug_design {

inline Json design_action_example(const std::string& action) {
    Json args = Json::object();
    if (action == "source.context") {
        args = {{"file", "rtl/top.sv"}, {"line", 42}};
    } else {
        args = {{"signal", "top.u.valid"}};
        if (action == "trace.driver" || action == "trace.load") {
            args["line_limit"] = 16;
        }
    }
    return Json{{"api_version", "xdebug.v1"},
                {"action", action},
                {"target", {{"session_id", "case_a"}}},
                {"args", args}};
}

inline Json design_missing_signal_error(const std::string& action) {
    return make_handler_error(
        "MISSING_FIELD",
        "args.signal is required",
        {{"invalid_arg", "args.signal"},
         {"expected", "design signal path or signal name accepted by the design database"},
         {"correct_example", design_action_example(action)},
         {"example_note", "Example only; replace target.session_id and args.signal with active design values."},
         {"next_actions", Json::array({"Use scope/list style discovery or signal.resolve to identify the signal path."})}});
}

} // namespace xdebug_design
