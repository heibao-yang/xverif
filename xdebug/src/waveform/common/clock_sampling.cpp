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

bool checked_add(npiFsdbTime base, long long offset, npiFsdbTime& out) {
    if (offset >= 0) {
        unsigned long long uoff = static_cast<unsigned long long>(offset);
        if (base > std::numeric_limits<npiFsdbTime>::max() - uoff) return false;
        out = base + uoff;
        return true;
    }
    unsigned long long abs_off = static_cast<unsigned long long>(-(offset + 1)) + 1ULL;
    if (base < abs_off) return false;
    out = base - abs_off;
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

bool parse_clock_sample_offset(npiFsdbFileHandle fsdb,
                               const std::string& text,
                               long long& ticks,
                               std::string& normalized_text,
                               std::string& error) {
    std::string value = trim_copy(text.empty() ? "0ns" : text);
    char* end = nullptr;
    double number = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || !std::isfinite(number)) {
        error = "invalid sample_offset: " + value;
        return false;
    }
    std::string unit = trim_copy(end ? end : "");
    if (unit.empty()) unit = "ns";
    double abs_number = std::fabs(number);
    npiFsdbTime abs_ticks = 0;
    if (!xdebug_core::convert_time(fsdb, abs_number, unit, abs_ticks, error)) {
        error = "invalid sample_offset: " + error;
        return false;
    }
    if (abs_ticks > static_cast<unsigned long long>(std::numeric_limits<long long>::max())) {
        error = "sample_offset is too large";
        return false;
    }
    ticks = static_cast<long long>(abs_ticks);
    if (number < 0) ticks = -ticks;
    normalized_text = (ticks == 0) ? "0ns" : value;
    return true;
}

bool normalize_clock_sample_spec(npiFsdbFileHandle fsdb,
                                 ClockSampleSpec& spec,
                                 std::string& error) {
    std::string normalized;
    if (!parse_clock_sample_offset(fsdb,
                                   spec.sample_offset_text.empty() ? "0ns" : spec.sample_offset_text,
                                   spec.sample_offset_ticks,
                                   normalized,
                                   error)) {
        return false;
    }
    spec.sample_offset_text = normalized;
    spec.zero_offset = spec.sample_offset_ticks == 0;
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

bool ClockSampleTimeResolver::sample_time_for_edge(npiFsdbTime edge_time, npiFsdbTime& sample_time) const {
    return checked_add(edge_time, spec_.sample_offset_ticks, sample_time);
}

bool ClockSampleTimeResolver::is_target_edge_at(npiFsdbTime time, ClockEdgeKind* edge_kind) const {
    if (!clock_handle_) return false;
    VctHandle vct(clock_handle_);
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
    bool matched = clock_edge_transition_matches(
        spec_.edge, truthy_one(previous_value), truthy_one(current_value), &actual);
    if (matched && edge_kind) *edge_kind = actual;
    return matched;
}

bool ClockSampleTimeResolver::next_single_edge_sample(ClockEdgeKind edge,
                                                       npiFsdbTime anchor_time,
                                                       ClockSamplePoint& point,
                                                       std::string& error) const {
    npiFsdbTime start_time = anchor_time;
    if (spec_.sample_offset_ticks > 0) {
        unsigned long long offset = static_cast<unsigned long long>(spec_.sample_offset_ticks);
        start_time = anchor_time > offset ? anchor_time - offset : 0;
    }

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
                npiFsdbTime sample_time = 0;
                if (sample_time_for_edge(change_time, sample_time) && sample_time >= anchor_time) {
                    point.edge_time = change_time;
                    point.sample_time = sample_time;
                    point.edge_kind = actual;
                    return true;
                }
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
