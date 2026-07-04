#include "clock_sampling.h"

#include "core/npi/time_contract.h"
#include "waveform/server/fsdb_scan_utils.h"

#include "npi_L1.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace xdebug_waveform {
namespace {

std::string trim_copy(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

std::string lower_copy(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool truthy_one(const std::string& value) {
    for (char c : value) {
        if (c == 'x' || c == 'X' || c == 'z' || c == 'Z') return false;
        if (c == '1') return true;
    }
    return false;
}

bool read_vct_value(npiFsdbVctHandle vct, std::string& out) {
    npiFsdbValue value;
    value.format = npiFsdbBinStrVal;
    if (!npi_fsdb_vct_value(vct, &value) || !value.value.str) return false;
    out = value.value.str;
    return true;
}

bool read_edge_at(npiFsdbSigHandle clock_handle, npiFsdbTime time, ClockEdgeKind* edge_kind) {
    if (!clock_handle) return false;
    VctHandle vct(clock_handle);
    if (!vct.valid()) return false;
    if (!npi_fsdb_goto_time(vct.get(), time)) return false;
    npiFsdbTime current_time = 0;
    if (!npi_fsdb_vct_time(vct.get(), &current_time) || current_time != time) return false;
    std::string current_value;
    if (!read_vct_value(vct.get(), current_value)) return false;
    if (!npi_fsdb_goto_prev(vct.get())) return false;
    std::string previous_value;
    if (!read_vct_value(vct.get(), previous_value)) return false;
    ClockEdgeKind actual = ClockEdgeKind::Negedge;
    if (!clock_edge_transition_matches(ClockEdgeKind::Dual,
                                       truthy_one(previous_value),
                                       truthy_one(current_value),
                                       &actual)) {
        return false;
    }
    if (edge_kind) *edge_kind = actual;
    return true;
}

} // namespace

const char* clock_edge_kind_text(ClockEdgeKind edge) {
    switch (edge) {
    case ClockEdgeKind::Posedge: return "posedge";
    case ClockEdgeKind::Negedge: return "negedge";
    case ClockEdgeKind::Dual: return "dual";
    }
    return "negedge";
}

const char* clock_sample_point_text(ClockSamplePointKind point) {
    switch (point) {
    case ClockSamplePointKind::Before: return "before";
    case ClockSamplePointKind::After: return "after";
    }
    return "before";
}

bool parse_clock_edge_kind(const std::string& text, ClockEdgeKind& edge, std::string& error) {
    std::string value = lower_copy(trim_copy(text));
    if (value.empty() || value == "negedge") {
        edge = ClockEdgeKind::Negedge;
        return true;
    }
    if (value == "posedge") {
        edge = ClockEdgeKind::Posedge;
        return true;
    }
    if (value == "dual") {
        edge = ClockEdgeKind::Dual;
        return true;
    }
    error = "invalid edge: " + text + "; expected posedge, negedge, or dual";
    return false;
}

bool parse_clock_sample_point_kind(const std::string& text,
                                   ClockSamplePointKind& point,
                                   std::string& error) {
    std::string value = lower_copy(trim_copy(text));
    if (value.empty() || value == "before") {
        point = ClockSamplePointKind::Before;
        return true;
    }
    if (value == "after") {
        point = ClockSamplePointKind::After;
        return true;
    }
    error = "invalid sample_point: " + text + "; expected before or after";
    return false;
}

bool clock_edge_transition_matches(ClockEdgeKind requested,
                                   bool old_one,
                                   bool new_one,
                                   ClockEdgeKind* actual) {
    if (!old_one && new_one) {
        if (actual) *actual = ClockEdgeKind::Posedge;
        return requested == ClockEdgeKind::Posedge || requested == ClockEdgeKind::Dual;
    }
    if (old_one && !new_one) {
        if (actual) *actual = ClockEdgeKind::Negedge;
        return requested == ClockEdgeKind::Negedge || requested == ClockEdgeKind::Dual;
    }
    return false;
}

bool normalize_clock_sample_spec(npiFsdbFileHandle fsdb,
                                 ClockSampleSpec& spec,
                                 std::string& error) {
    (void)fsdb;
    if (spec.edge == ClockEdgeKind::Negedge && spec.has_sample_point) {
        error = "sample_point is only valid with edge:posedge or edge:dual";
        return false;
    }
    return true;
}

ClockSampleTimeResolver::ClockSampleTimeResolver(npiFsdbFileHandle fsdb, const ClockSampleSpec& spec)
    : fsdb_(fsdb), spec_(spec) {
    if (fsdb_ && !spec_.clock.empty()) {
        clock_handle_ = npi_fsdb_sig_by_name(fsdb_, spec_.clock.c_str(), nullptr);
    }
}

bool ClockSampleTimeResolver::valid(std::string& error) const {
    if (!fsdb_) {
        error = "FSDB handle is not available";
        return false;
    }
    if (spec_.clock.empty()) {
        error = "clock is required";
        return false;
    }
    if (!clock_handle_) {
        error = "Clock signal not found: " + spec_.clock;
        return false;
    }
    return true;
}

bool ClockSampleTimeResolver::is_clock_edge_at(npiFsdbTime time, ClockEdgeKind* edge_kind) const {
    return read_edge_at(clock_handle_, time, edge_kind);
}

bool ClockSampleTimeResolver::is_target_edge_at(npiFsdbTime time, ClockEdgeKind* edge_kind) const {
    ClockEdgeKind actual = ClockEdgeKind::Negedge;
    bool have_edge = is_clock_edge_at(time, &actual);
    bool matched = have_edge &&
        (spec_.edge == ClockEdgeKind::Dual || spec_.edge == actual);
    if (matched && edge_kind) *edge_kind = actual;
    return matched;
}

bool ClockSampleTimeResolver::previous_single_edge_sample(ClockEdgeKind edge,
                                                          npiFsdbTime anchor_time,
                                                          ClockSamplePoint& point,
                                                          std::string& error) const {
    if (!clock_handle_) {
        error = "Clock signal not found: " + spec_.clock;
        return false;
    }
    ClockEdgeCursor cursor(clock_handle_, edge == ClockEdgeKind::Posedge);
    if (!cursor.valid()) {
        error = "failed to create clock edge cursor for " + spec_.clock;
        return false;
    }
    npiFsdbTime t = 0;
    if (!cursor.prev_before(anchor_time, t)) return false;
    point.edge_time = t;
    point.sample_time = t;
    point.edge_kind = edge;
    return true;
}

bool ClockSampleTimeResolver::next_single_edge_sample(ClockEdgeKind edge,
                                                       npiFsdbTime anchor_time,
                                                       ClockSamplePoint& point,
                                                       std::string& error) const {
    npiFsdbTime start_time = anchor_time;

    SignalChangeCursor cursor(clock_handle_, npiFsdbBinStrVal);
    if (!cursor.valid()) {
        error = "failed to create clock value cursor for " + spec_.clock;
        return false;
    }

    npiFsdbTime previous_time = 0;
    std::string previous_value;
    bool have_previous = cursor.prev_before(start_time, previous_time, previous_value);

    npiFsdbTime change_time = 0;
    std::string current_value;
    if (!cursor.first_at_or_after(start_time, change_time, current_value)) return false;
    while (true) {
        if (change_time < start_time) {
            previous_value = current_value;
            have_previous = true;
        } else if (have_previous) {
            ClockEdgeKind actual = ClockEdgeKind::Negedge;
            if (clock_edge_transition_matches(edge,
                                              truthy_one(previous_value),
                                              truthy_one(current_value),
                                              &actual)) {
                point.edge_time = change_time;
                point.sample_time = change_time;
                point.edge_kind = actual;
                return true;
            }
            previous_value = current_value;
            have_previous = true;
        } else {
            previous_value = current_value;
            have_previous = true;
        }
        if (!cursor.next(change_time, current_value)) return false;
    }
}

bool ClockSampleTimeResolver::find_next_sample(npiFsdbTime anchor_time,
                                                ClockSamplePoint& point,
                                                std::string& error) const {
    if (!valid(error)) return false;
    if (spec_.edge != ClockEdgeKind::Dual) {
        return next_single_edge_sample(spec_.edge, anchor_time, point, error);
    }
    ClockSamplePoint pos;
    ClockSamplePoint neg;
    std::string pos_error;
    std::string neg_error;
    bool have_pos = next_single_edge_sample(ClockEdgeKind::Posedge, anchor_time, pos, pos_error);
    bool have_neg = next_single_edge_sample(ClockEdgeKind::Negedge, anchor_time, neg, neg_error);
    if (!have_pos && !have_neg) {
        error = pos_error.empty() ? neg_error : pos_error;
        return false;
    }
    if (!have_neg || (have_pos && pos.sample_time <= neg.sample_time)) point = pos;
    else point = neg;
    return true;
}

bool ClockSampleTimeResolver::find_previous_sample(npiFsdbTime anchor_time,
                                                   ClockSamplePoint& point,
                                                   std::string& error) const {
    if (!valid(error)) return false;
    if (spec_.edge != ClockEdgeKind::Dual) {
        return previous_single_edge_sample(spec_.edge, anchor_time, point, error);
    }
    ClockSamplePoint pos;
    ClockSamplePoint neg;
    std::string pos_error;
    std::string neg_error;
    bool have_pos = previous_single_edge_sample(ClockEdgeKind::Posedge, anchor_time, pos, pos_error);
    bool have_neg = previous_single_edge_sample(ClockEdgeKind::Negedge, anchor_time, neg, neg_error);
    if (!have_pos && !have_neg) {
        error = pos_error.empty() ? neg_error : pos_error;
        return false;
    }
    if (!have_neg || (have_pos && pos.sample_time >= neg.sample_time)) point = pos;
    else point = neg;
    return true;
}

bool ClockSampleTimeResolver::for_each_sample_time(
    npiFsdbTime begin,
    npiFsdbTime end,
    const std::function<bool(const ClockSamplePoint&)>& callback,
    std::string& error) const {
    if (!valid(error)) return false;
    npiFsdbTime anchor = begin;
    while (true) {
        ClockSamplePoint point;
        if (!find_next_sample(anchor, point, error)) return true;
        if (point.sample_time > end) return true;
        if (!callback(point)) return false;
        if (point.edge_time == std::numeric_limits<npiFsdbTime>::max() ||
            point.sample_time == std::numeric_limits<npiFsdbTime>::max()) {
            return true;
        }
        npiFsdbTime next_edge_anchor = point.edge_time + 1;
        npiFsdbTime next_sample_anchor = point.sample_time + 1;
        anchor = std::max(next_edge_anchor, next_sample_anchor);
    }
}

} // namespace xdebug_waveform
