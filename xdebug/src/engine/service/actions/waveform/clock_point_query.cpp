#include "clock_point_query.h"

#include "core/npi/time_contract.h"
#include "waveform/server/fsdb_scan_utils.h"
#include "waveform/value/logic_value.h"

#include "npi_L1.h"

#include <cctype>

namespace xdebug_design {
namespace {

using xdebug_waveform::ClockEdgeKind;
using xdebug_waveform::ClockSamplePoint;
using xdebug_waveform::ClockSamplePointKind;

Json err(const char* code, const std::string& message) {
    return Json{{"error", code}, {"message", message}};
}

Json invalid_arg(const std::string& arg, const std::string& expected) {
    return Json{{"error", "INVALID_REQUEST"},
                {"invalid_arg", arg},
                {"expected", expected},
                {"message", "invalid clock sampling argument: " + arg}};
}

std::string trim_copy(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

std::string with_value_prefix(const std::string& raw, char fmt) {
    std::string text = trim_copy(raw);
    if (text.size() >= 2 && text[0] == '\'') return text;
    return std::string("'") + static_cast<char>(std::tolower(static_cast<unsigned char>(fmt))) + text;
}

Json value_object(const std::string& raw, char fmt) {
    return xdebug_waveform::logic_value_json(
        xdebug_waveform::logic_value_from_fsdb_raw(raw, fmt));
}

bool read_current_at(const PointSignalSpec& sig,
                     npiFsdbFileHandle fsdb,
                     npiFsdbTime time,
                     npiFsdbValType value_type,
                     char prefix,
                     Json& cell) {
    std::string raw;
    if (!npi_fsdb_sig_value_at(fsdb,
                               sig.path.c_str(),
                               time,
                               raw,
                               value_type)) {
        cell = {{"status", "signal_not_found"}, {"value", nullptr}};
        return false;
    }
    raw = with_value_prefix(raw, prefix);
    cell = {{"status", "ok"}, {"value", value_object(raw, prefix)}};
    return true;
}

bool read_before_at(const PointSignalSpec& sig,
                    npiFsdbFileHandle fsdb,
                    npiFsdbTime time,
                    npiFsdbValType value_type,
                    char prefix,
                    Json& cell) {
    npiFsdbSigHandle h = npi_fsdb_sig_by_name(fsdb, sig.path.c_str(), nullptr);
    if (!h) {
        cell = {{"status", "signal_not_found"}, {"value", nullptr}};
        return false;
    }
    xdebug_waveform::SignalChangeCursor cursor(h, value_type);
    npiFsdbTime value_time = 0;
    std::string raw;
    if (!cursor.prev_before(time, value_time, raw)) {
        cell = {{"status", "missing_value"}, {"value", nullptr}};
        return false;
    }
    raw = with_value_prefix(raw, prefix);
    cell = {{"status", "ok"},
            {"time", xdebug_core::format_time(fsdb, value_time)},
            {"value", value_object(raw, prefix)}};
    return true;
}

Json missing_edge_cell() {
    return Json{{"status", "missing_edge"}, {"value", nullptr}};
}

Json sample_values_at(const std::vector<PointSignalSpec>& signals,
                      npiFsdbFileHandle fsdb,
                      npiFsdbTime time,
                      bool before,
                      npiFsdbValType value_type,
                      char prefix) {
    Json rows = Json::array();
    for (const auto& sig : signals) {
        Json cell;
        if (before) read_before_at(sig, fsdb, time, value_type, prefix, cell);
        else read_current_at(sig, fsdb, time, value_type, prefix, cell);
        Json row = {{"signal", sig.label.empty() ? sig.path : sig.label},
                    {"path", sig.path},
                    {"time", xdebug_core::format_time(fsdb, time)},
                    {"cell", cell}};
        rows.push_back(row);
    }
    return rows;
}

Json cell_for_signal(const Json& sample_rows, const std::string& path) {
    if (!sample_rows.is_array()) return missing_edge_cell();
    for (const auto& row : sample_rows) {
        if (row.value("path", std::string()) == path) return row.value("cell", Json::object());
    }
    return Json{{"status", "signal_not_found"}, {"value", nullptr}};
}

} // namespace

bool parse_point_clock_args(const Json& args,
                            xdebug_waveform::ClockSampleSpec& spec,
                            Json& error) {
    static const char* legacy[] = {"clk", "sampling", "clock_edge", "posedge", "sample_offset", nullptr};
    for (int i = 0; legacy[i]; ++i) {
        if (args.contains(legacy[i])) {
            error = invalid_arg(std::string("args.") + legacy[i],
                                "use args.clock, args.edge, and args.sample_point");
            return false;
        }
    }
    spec = xdebug_waveform::ClockSampleSpec();
    if (!args.contains("clock") || !args["clock"].is_string() || args["clock"].get<std::string>().empty()) {
        error = err("MISSING_FIELD", "args.clock is required");
        return false;
    }
    spec.clock = args["clock"].get<std::string>();
    std::string parse_error;
    if (!xdebug_waveform::parse_clock_edge_kind(args.value("edge", std::string("negedge")),
                                                spec.edge,
                                                parse_error)) {
        error = invalid_arg("args.edge", "posedge, negedge, or dual");
        error["message"] = parse_error;
        return false;
    }
    if (args.contains("sample_point")) {
        if (!args["sample_point"].is_string()) {
            error = invalid_arg("args.sample_point", "before or after");
            return false;
        }
        spec.has_sample_point = true;
        if (!xdebug_waveform::parse_clock_sample_point_kind(args["sample_point"].get<std::string>(),
                                                            spec.sample_point,
                                                            parse_error)) {
            error = invalid_arg("args.sample_point", "before or after");
            error["message"] = parse_error;
            return false;
        }
    }
    if (!xdebug_waveform::normalize_clock_sample_spec(nullptr, spec, parse_error)) {
        error = invalid_arg("args.sample_point", "only valid with edge:posedge or edge:dual");
        error["message"] = parse_error;
        return false;
    }
    return true;
}

bool build_clock_point_query(npiFsdbFileHandle fsdb,
                             const xdebug_waveform::ClockSampleSpec& spec,
                             npiFsdbTime requested_time,
                             const std::string& formatted_requested_time,
                             const std::vector<PointSignalSpec>& signals,
                             npiFsdbValType value_type,
                             char value_prefix,
                             ClockPointQueryResult& out,
                             Json& error) {
    xdebug_waveform::ClockSampleTimeResolver resolver(fsdb, spec);
    std::string resolver_error;
    if (!resolver.valid(resolver_error)) {
        error = err("INVALID_REQUEST", resolver_error);
        return false;
    }

    ClockEdgeKind actual_edge = ClockEdgeKind::Negedge;
    bool clock_edge_hit = resolver.is_clock_edge_at(requested_time, &actual_edge);
    ClockEdgeKind target_kind = ClockEdgeKind::Negedge;
    bool target_edge_hit = resolver.is_target_edge_at(requested_time, &target_kind);

    ClockSamplePoint prev_point;
    ClockSamplePoint next_point;
    bool have_prev = resolver.find_previous_sample(requested_time, prev_point, resolver_error);
    bool have_next = resolver.find_next_sample(requested_time + 1, next_point, resolver_error);
    bool edge_sample_before =
        spec.edge != ClockEdgeKind::Negedge &&
        spec.sample_point == ClockSamplePointKind::Before;

    Json before_rows = have_prev
        ? sample_values_at(signals, fsdb, prev_point.sample_time, edge_sample_before, value_type, value_prefix)
        : Json::array();
    bool middle_before = target_edge_hit &&
        edge_sample_before;
    Json middle_rows = sample_values_at(signals, fsdb, requested_time, middle_before, value_type, value_prefix);
    Json after_rows = have_next
        ? sample_values_at(signals, fsdb, next_point.sample_time, edge_sample_before, value_type, value_prefix)
        : Json::array();

    Json rows = Json::array();
    for (const auto& sig : signals) {
        Json row;
        row["signal"] = sig.label.empty() ? sig.path : sig.label;
        row["path"] = sig.path;
        row["before"] = have_prev ? cell_for_signal(before_rows, sig.path) : missing_edge_cell();
        row["middle"] = cell_for_signal(middle_rows, sig.path);
        row["after"] = have_next ? cell_for_signal(after_rows, sig.path) : missing_edge_cell();
        rows.push_back(row);
    }

    Json context;
    context["clock"] = spec.clock;
    context["edge"] = xdebug_waveform::clock_edge_kind_text(spec.edge);
    context["requested_time"] = formatted_requested_time;
    context["clock_edge_hit"] = clock_edge_hit;
    context["clock_edge_kind"] = clock_edge_hit ? Json(xdebug_waveform::clock_edge_kind_text(actual_edge)) : Json(nullptr);
    context["target_edge_hit"] = target_edge_hit;
    context["sample_point_applied"] =
        target_edge_hit && spec.edge != ClockEdgeKind::Negedge
        ? Json(xdebug_waveform::clock_sample_point_text(spec.sample_point))
        : Json(nullptr);
    context["previous_sample_time"] = have_prev
        ? Json(xdebug_core::format_time(fsdb, prev_point.sample_time)) : Json(nullptr);
    context["next_sample_time"] = have_next
        ? Json(xdebug_core::format_time(fsdb, next_point.sample_time)) : Json(nullptr);
    context["bracket_complete"] = have_prev && have_next;

    out.clock_context = context;
    out.rows = rows;
    out.samples = {{"before", before_rows}, {"middle", middle_rows}, {"after", after_rows}};
    return true;
}

Json point_cell_value(const Json& row, const char* key) {
    if (!row.contains(key) || !row[key].is_object()) return Json();
    const Json& cell = row[key];
    if (cell.value("status", std::string()) != "ok") return cell.value("status", std::string());
    return cell.value("value", Json());
}

} // namespace xdebug_design
