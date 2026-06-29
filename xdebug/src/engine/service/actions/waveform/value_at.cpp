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

static bool contains_xz(const std::string& v) {
    return xdebug_waveform::logic_value_has_xz(
        xdebug_waveform::logic_value_from_fsdb_raw(v, 'h'));
}
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
static Json make_xbit_hints(const Json& args,
                            const std::string& signal,
                            const std::string& raw) {
    if (!args.contains("slice_hint") || !args["slice_hint"].is_object()) return Json();
    Json hint = args["slice_hint"];
    long long width = hint.value("chunk_width", 0LL);
    long long count = hint.value("count", 1LL);
    if (width <= 0) {
        return Json{{"status", "needs_slice_hint"}, {"signal", signal},
                    {"raw_value", trim_copy(raw)}};
    }
    if (count <= 0) count = 1;
    Json slices = Json::array();
    Json commands = Json::array();
    for (long long i = 0; i < count; ++i) {
        long long lo = i * width;
        long long hi = lo + width - 1;
        slices.push_back({{"index", i},
                          {"range", "[" + std::to_string(hi) + ":" + std::to_string(lo) + "]"}});
        commands.push_back("tools/xbit slice \"" + trim_copy(raw) + "\" " +
                           std::to_string(hi) + " " + std::to_string(lo) + " --json");
    }
    return Json{{"status", "ready"}, {"signal", signal}, {"raw_value", trim_copy(raw)},
                {"chunk_width", width}, {"count", count}, {"slices", slices},
                {"commands", commands}};
}

class ValueAtHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "value.at"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& request, EngineActionContext& ctx) const override {
        Json args = request.value("args", Json::object());
        std::string signal = args.value("signal", std::string());
        std::string time_str = args.value("time", args.value("at", std::string()));
        std::string fmt_str = args.value("format", std::string("h"));
        if (signal.empty() || time_str.empty())
            return err("MISSING_FIELD", "args.signal and args.time are required");

        char fmt = static_cast<char>(std::tolower(
            static_cast<unsigned char>(fmt_str.empty() ? 'h' : fmt_str[0])));
        if (fmt != 'h' && fmt != 'b' && fmt != 'd') fmt = 'h';

        npiFsdbTime fsdb_time = 0;
        std::string time_error;
        if (!xdebug_waveform::parse_user_time(time_str.c_str(), false, fsdb_time, time_error))
            return err("TIME_SPEC_INVALID", time_error.empty() ? "failed to parse time: " + time_str : time_error);

        npiFsdbValType vtype = xdebug_waveform::parse_format(fmt);
        std::string raw;
        if (!npi_fsdb_sig_value_at(g_fsdb_file, signal.c_str(), fsdb_time, raw, vtype))
            return err("SIGNAL_NOT_FOUND", "failed to read value: " + signal);
        raw = with_value_prefix(raw, fmt);

        Json out;
        out["signal"] = signal;
        std::string formatted_time = xdebug_core::format_time(g_fsdb_file, fsdb_time);
        out["time"] = formatted_time;
        std::string requested_format = args.value("format", "");
        if (requested_format == "array_indexed" && raw.find('{') == std::string::npos) {
            out["status"] = "unsupported_format";
            out["value"] = value_object(raw);
            out["raw_value"] = trim_copy(raw);
            out["reason"] = "format:array_indexed requires an FSDB aggregate value";
        } else {
            out["status"] = "ok";
            out["value"] = value_object(raw);
        }
        out["known"] = !contains_xz(raw);
        Json hints = make_xbit_hints(args, signal, raw);
        if (!hints.is_null()) out["xbit_hints"] = hints;
        out["summary"] = {{"signal", signal}, {"time", formatted_time},
                          {"known", !contains_xz(raw)}, {"status", out["status"]}};
        return out;
    }

private:
    static Json err(const char* code, const std::string& msg) {
        Json e; e["error"] = code; e["message"] = msg; return e;
    }
    std::string render_xout(const Json& r) const override {
        xdebug::TextResponseBuilder out("xdebug");
        out.emit_header(action_name());
        const Json& d = r.value("data", Json::object());
        out.emit_section("target");
        if (d.contains("signal")) out.emit_kv("signal", d["signal"]);
        if (d.contains("time")) out.emit_kv("time", d["time"]);
        out.emit_section("summary");
        if (d.contains("status")) out.emit_kv("status", d["status"]);
        if (d.contains("value")) out.emit_kv("value", d["value"]);
        return out.str();
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_value_at_handler() {
    return std::unique_ptr<EngineActionHandler>(new ValueAtHandler);
}

}  // namespace xdebug_design
