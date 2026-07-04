#include "service/engine_action_handler.h"
#include "service/engine_action_registry.h"
#include "service/engine_globals.h"
#include "clock_point_query.h"

#include "api/text_response_builder.h"
#include "design/protocol/protocol.h"
#include "waveform/server/fsdb_value_reader.h"
#include "waveform/event/event_manager.h"
#include "waveform/event/event_analyzer.h"
#include "waveform/list/list_manager.h"
#include "waveform/list/signal_list.h"
#include "waveform/export/waveform_exporter.h"
#include "waveform/common/xdebug_waveform_paths.h"
#include "waveform/service/action_support.h"
#include "waveform/service/rc_generator.h"
#include "waveform/value/logic_value.h"
#include "core/npi/time_contract.h"

#include "npi.h"
#include "npi_fsdb.h"
#include "npi_L1.h"
#include "npi_hdl.h"

#include <fstream>
#include <memory>
#include <algorithm>
#include <map>
#include <sstream>
#include <vector>

namespace xdebug_design {
namespace {

class VerifyConditionsHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "verify.conditions"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& request, EngineActionContext& ctx) const override {
        Json args = request.value("args", Json::object());
        Json conditions = args.value("conditions", Json::array());
        std::string time_str = args.value("time", args.value("at", ""));
        if (time_str.empty() || !conditions.is_array())
            return err("MISSING_FIELD", "args.conditions[] and args.time are required");
        xdebug_waveform::ClockSampleSpec clock_spec;
        Json clock_error;
        if (!parse_point_clock_args(args, clock_spec, clock_error)) return clock_error;

        npiFsdbTime fsdb_time = 0;
        std::string time_error;
        if (!xdebug_waveform::parse_user_time(time_str.c_str(), false, fsdb_time, time_error))
            return err("TIME_SPEC_INVALID", time_error.empty() ? time_str : time_error);
        std::string formatted_time = xdebug_core::format_time(g_fsdb_file, fsdb_time);
        std::vector<PointSignalSpec> signal_specs;
        for (auto& cond : conditions) {
            std::string signal = cond.value("signal", "");
            if (!signal.empty()) signal_specs.push_back({signal, signal});
        }
        ClockPointQueryResult point;
        Json point_error;
        if (!build_clock_point_query(g_fsdb_file,
                                     clock_spec,
                                     fsdb_time,
                                     formatted_time,
                                     signal_specs,
                                     npiFsdbHexStrVal,
                                     'h',
                                     point,
                                     point_error)) {
            return point_error;
        }

        Json results = Json::array();
        int passed = 0;
        int failed = 0;
        int unknown = 0;
        for (auto& cond : conditions) {
            Json r;
            r["signal"] = cond.value("signal", "");
            r["time"] = formatted_time;
            r["op"] = cond.value("op", "==");
            r["expected"] = cond.value("value", "");
            xdebug_waveform::LogicValue expected_value =
                xdebug_waveform::parse_user_logic_literal(r["expected"].get<std::string>());
            if (!expected_value.valid)
                return err("VALUE_FORMAT_INVALID", expected_value.error);
            std::string signal = cond.value("signal", "");
            if (!signal.empty()) {
                Json sample_row = Json::object();
                for (const auto& row : point.rows) {
                    if (row.value("path", std::string()) == signal) {
                        sample_row = row;
                        break;
                    }
                }
                Json middle = sample_row.value("middle", Json::object());
                if (middle.value("status", std::string()) == "ok") {
                    Json observed_json = middle.value("value", Json::object());
                    std::string raw = observed_json.value("value", std::string());
                    xdebug_waveform::LogicValue observed =
                        xdebug_waveform::logic_value_from_fsdb_raw(raw, 'h');
                    bool known = !xdebug_waveform::logic_value_has_xz(observed) &&
                                 !xdebug_waveform::logic_value_has_xz(expected_value);
                    r["observed"] = observed_json;
                    r["samples"] = sample_row;
                    r["known"] = known;
                    std::string op = cond.value("op", "==");
                    if (known) {
                        bool equal = xdebug_waveform::logic_value_compare_key(observed) ==
                                     xdebug_waveform::logic_value_compare_key(expected_value);
                        bool pass = op == "!=" ? !equal : equal;
                        r["status"] = pass ? "pass" : "fail";
                        r["pass"] = pass;
                        if (pass) passed++; else failed++;
                    } else {
                        r["status"] = "unknown";
                        r["pass"] = nullptr;
                        unknown++;
                    }
                } else {
                    r["known"] = false;
                    r["status"] = "unknown";
                    r["pass"] = nullptr;
                    r["error"] = middle.value("status", std::string("signal not found"));
                    r["samples"] = sample_row;
                    unknown++;
                }
            } else {
                r["known"] = false;
                r["status"] = "unknown";
                r["pass"] = nullptr;
                unknown++;
            }
            results.push_back(r);
        }
        Json out;
        bool all_passed = failed == 0 && unknown == 0;
        out["summary"] = {
            {"time", formatted_time},
            {"verdict", all_passed ? "pass" : "fail"},
            {"condition_count", results.size()},
            {"all_passed", all_passed},
            {"passed", passed},
            {"failed", failed},
            {"unknown", unknown}
        };
        out["summary"]["clock_edge_hit"] = point.clock_context["clock_edge_hit"];
        out["summary"]["target_edge_hit"] = point.clock_context["target_edge_hit"];
        out["summary"]["bracket_complete"] = point.clock_context["bracket_complete"];
        out["checks"] = results;
        out["clock_context"] = point.clock_context;
        out["sample_rows"] = point.rows;
        return out;
    }
private:
    static Json err(const char* c, const std::string& m) {
        Json e; e["error"] = c; e["message"] = m; return e;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_verify_conditions_handler() {
    return std::unique_ptr<EngineActionHandler>(new VerifyConditionsHandler);
}

}  // namespace xdebug_design
