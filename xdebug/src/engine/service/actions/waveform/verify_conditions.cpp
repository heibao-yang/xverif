#include "service/engine_action_handler.h"
#include "service/engine_action_registry.h"
#include "service/engine_globals.h"

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

static std::string trim_copy(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}
static Json value_object(const std::string& raw) {
    return xdebug_waveform::logic_value_json(
        xdebug_waveform::logic_value_from_fsdb_raw(raw, 'h'));
}
static std::string with_value_prefix(const std::string& raw, char fmt) {
    std::string text = trim_copy(raw);
    if (text.size() >= 2 && text[0] == '\'') return text;
    return std::string("'") + static_cast<char>(std::tolower(static_cast<unsigned char>(fmt))) + text;
}

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

        npiFsdbTime fsdb_time = 0;
        std::string time_error;
        if (!xdebug_waveform::parse_user_time(time_str.c_str(), false, fsdb_time, time_error))
            return err("TIME_SPEC_INVALID", time_error.empty() ? time_str : time_error);
        std::string formatted_time = xdebug_core::format_time(g_fsdb_file, fsdb_time);

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
                std::string raw;
                if (npi_fsdb_sig_value_at(g_fsdb_file, signal.c_str(), fsdb_time, raw, npiFsdbHexStrVal)) {
                    raw = with_value_prefix(raw, 'h');
                    xdebug_waveform::LogicValue observed =
                        xdebug_waveform::logic_value_from_fsdb_raw(raw, 'h');
                    bool known = !xdebug_waveform::logic_value_has_xz(observed) &&
                                 !xdebug_waveform::logic_value_has_xz(expected_value);
                    r["observed"] = value_object(raw);
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
                    r["error"] = "signal not found";
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
        out["checks"] = results;
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
