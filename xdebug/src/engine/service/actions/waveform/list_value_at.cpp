#include "service/engine_action_handler.h"
#include "service/engine_action_registry.h"
#include "service/engine_globals.h"
#include "waveform_action_support.h"
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
class ListValueAtHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "list.value_at"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& r, EngineActionContext& ctx) const override {
        Json a = r.value("args", Json::object());
        std::string n = a.value("name", ""), ts = a.value("time", "");
        if (n.empty() || ts.empty())
            return Json({{"error","MISSING_FIELD"},{"message","args.name+time"}});
        xdebug_waveform::ClockSampleSpec clock_spec;
        Json clock_error;
        if (!parse_point_clock_args(a, clock_spec, clock_error)) return clock_error;
        xdebug_waveform::SignalList lst;
        if (!read_list_storage(n, lst))
            return Json({{"error","LIST_NOT_FOUND"},{"message",n}});
        npiFsdbTime ft = 0;
        std::string time_error;
        if (!xdebug_waveform::parse_user_time(ts.c_str(), false, ft, time_error))
            return Json({{"error","TIME_SPEC_INVALID"},{"message",time_error}});
        std::string formatted_time = xdebug_core::format_time(xdebug_waveform::g_fsdb_file, ft);
        std::vector<PointSignalSpec> specs;
        for (const auto& sig : lst.signals) specs.push_back({sig, sig});
        ClockPointQueryResult point;
        Json point_error;
        if (!build_clock_point_query(xdebug_waveform::g_fsdb_file,
                                     clock_spec,
                                     ft,
                                     formatted_time,
                                     specs,
                                     npiFsdbHexStrVal,
                                     'h',
                                     point,
                                     point_error)) {
            return point_error;
        }
        Json out;
        out["summary"] = {{"name", n}, {"time", formatted_time},
                          {"signal_count", static_cast<int>(lst.signals.size())},
                          {"clock_edge_hit", point.clock_context["clock_edge_hit"]},
                          {"target_edge_hit", point.clock_context["target_edge_hit"]},
                          {"bracket_complete", point.clock_context["bracket_complete"]}};
        Json sv = Json::object();
        for (size_t i = 0; i < lst.signals.size() && i < point.rows.size(); i++) {
            Json middle = point.rows[i].value("middle", Json::object());
            sv[lst.signals[i]] = middle.value("status", std::string()) == "ok"
                ? middle.value("value", Json()) : Json(middle.value("status", std::string("NOT_FOUND")));
        }
        out["values"] = sv;
        out["sample_rows"] = point.rows;
        out["samples"] = point.samples;
        out["clock_context"] = point.clock_context;
        return out;
    }
    std::string render_xout(const Json& r) const override {
        xdebug::TextResponseBuilder out("xdebug");
        out.emit_header(action_name());
        const Json& d = r.value("data", Json::object());
        const Json& s = r.value("summary", Json::object());
        out.emit_section("target");
        if (s.contains("name")) out.emit_kv("name", s["name"]);
        if (s.contains("time")) out.emit_kv("time", s["time"]);
        out.emit_section("values");
        std::vector<std::vector<std::string>> rows;
        if (d.contains("sample_rows") && d["sample_rows"].is_array()) {
            for (const auto& row : d["sample_rows"]) {
                rows.push_back({row.value("signal", std::string()),
                                xdebug::json_to_xout_value(point_cell_value(row, "before")),
                                xdebug::json_to_xout_value(point_cell_value(row, "middle")),
                                xdebug::json_to_xout_value(point_cell_value(row, "after"))});
            }
        }
        out.emit_table({"signal", "before", "middle", "after"}, rows);
        return out.str();
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_list_value_at_handler() {
    return std::unique_ptr<EngineActionHandler>(new ListValueAtHandler);
}

}  // namespace xdebug_design
