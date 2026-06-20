#include "stream_analyzer.h"

#include "../server/fsdb_scan_utils.h"
#include "../server/fsdb_value_reader.h"

#include "npi_fsdb.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>

namespace xdebug_waveform {

std::string format_time(npiFsdbTime t);

namespace {

void issue(std::vector<StreamValidationIssue>& issues,
           const std::string& severity,
           const std::string& code,
           const std::string& message) {
    issues.push_back(StreamValidationIssue{severity, code, message});
}

std::string plain_clock_signal(const StreamConfig& config) {
    return config.clock;
}

std::vector<std::string> sorted_signals(const std::set<std::string>& signals) {
    return std::vector<std::string>(signals.begin(), signals.end());
}

StreamValue x_value() {
    return StreamValue{"x", false};
}

unsigned long long parse_u64(const std::string& text, bool& ok) {
    std::string s;
    for (char c : text) if (c != '_') s.push_back(c);
    int base = 10;
    const char* start = s.c_str();
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        start += 2;
    }
    char* end = nullptr;
    unsigned long long out = std::strtoull(start, &end, base);
    ok = end && *end == '\0';
    return out;
}

bool value_u64(const StreamValue& value, unsigned long long& out) {
    if (!value.known || value.bits.size() > 63) return false;
    out = 0;
    for (char c : value.bits) {
        out <<= 1ULL;
        if (c == '1') out |= 1ULL;
        else if (c != '0') return false;
    }
    return true;
}

Json fields_json(const std::map<std::string, StreamValue>& fields) {
    Json out = Json::object();
    for (const auto& kv : fields) out[kv.first] = stream_value_json(kv.second);
    return out;
}

} // namespace

struct StreamAnalyzer::Compiled {
    StreamExpression clock;
    StreamExpression reset;
    StreamExpression vld;
    StreamExpression rdy;
    StreamExpression bp;
    StreamExpression sop;
    StreamExpression eop;
    StreamExpression data;
    StreamExpression channel;
    std::map<std::string, StreamExpression> data_fields;
    std::set<std::string> deps;
    std::string clock_signal;
};

bool StreamAnalyzer::compile(npiFsdbFileHandle file, const StreamConfig& config, Compiled& c,
                             std::vector<StreamValidationIssue>* issues, std::string& error) {
    auto parse_one = [&](const std::string& label, const std::string& text, StreamExpression& expr) -> bool {
        if (text.empty()) return true;
        std::string parse_error;
        if (!expr.parse(text, parse_error)) {
            error = label + " expression parse failed: " + parse_error;
            return false;
        }
        for (const auto& sig : expr.signals()) c.deps.insert(sig);
        return true;
    };

    if (!parse_one("clock", config.clock, c.clock)) return false;
    if (!c.clock.is_plain_signal()) {
        if (issues) issue(*issues, "WARNING", "CLOCK_COMPLEX",
            "clock expression is not a plain signal; edge detection uses expression dependency changes");
    }
    c.clock_signal = plain_clock_signal(config);
    if (!parse_one("reset", config.reset, c.reset)) return false;
    if (!parse_one("vld", config.vld, c.vld)) return false;
    if (!parse_one("rdy", config.rdy, c.rdy)) return false;
    if (!parse_one("bp", config.bp, c.bp)) return false;
    if (!parse_one("sop", config.sop, c.sop)) return false;
    if (!parse_one("eop", config.eop, c.eop)) return false;
    if (!parse_one("data", config.data, c.data)) return false;
    if (!parse_one("channel_id", config.channel_id, c.channel)) return false;
    for (const auto& kv : config.data_fields) {
        StreamExpression expr;
        if (!parse_one("data_fields." + kv.first, kv.second, expr)) return false;
        c.data_fields[kv.first] = std::move(expr);
    }

    for (const auto& sig : c.deps) {
        if (!npi_fsdb_sig_by_name(file, sig.c_str(), NULL)) {
            error = "signal not found: " + sig;
            return false;
        }
    }
    return true;
}

bool StreamAnalyzer::validate_static(npiFsdbFileHandle file, const StreamConfig& config,
                                     std::vector<StreamValidationIssue>& issues,
                                     std::string& error) {
    issues.clear();
    if (!stream_name_valid(config.name)) issue(issues, "ERROR", "INVALID_NAME", "invalid stream name");
    if (config.data.empty() && config.data_fields.empty())
        issue(issues, "ERROR", "MISSING_DATA", "stream requires data or data_fields");
    if ((config.sop.empty()) != (config.eop.empty()))
        issue(issues, "ERROR", "PACKET_PAIR", "sop/eop must be configured together");

    Compiled compiled;
    if (!compile(file, config, compiled, &issues, error)) {
        issue(issues, "ERROR", "COMPILE_FAILED", error);
        return false;
    }
    return true;
}

bool StreamAnalyzer::analyze(npiFsdbFileHandle file, const StreamConfig& config,
                             const StreamQueryOptions& options, StreamAnalysis& analysis,
                             std::string& error) {
    analysis = StreamAnalysis();
    Compiled compiled;
    if (!compile(file, config, compiled, nullptr, error)) return false;
    std::vector<std::string> deps = sorted_signals(compiled.deps);
    std::vector<std::string> clock_deps = sorted_signals(compiled.clock.signals());
    if (clock_deps.empty()) {
        error = "clock expression has no signal dependency";
        return false;
    }
    std::vector<npiFsdbTime> edge_times;
    std::set<npiFsdbTime> candidate_times;
    for (const auto& sig_name : clock_deps) {
        npiFsdbSigHandle sig = npi_fsdb_sig_by_name(file, sig_name.c_str(), NULL);
        SignalChangeCursor cursor(sig, npiFsdbBinStrVal);
        if (!cursor.valid()) {
            error = "failed to create clock dependency cursor: " + sig_name;
            return false;
        }
        npiFsdbTime change_time = 0;
        std::string raw_value;
        if (!cursor.first_at_or_after(options.begin, change_time, raw_value)) continue;
        if (change_time >= options.begin && change_time <= options.end) candidate_times.insert(change_time);
        while (cursor.next(change_time, raw_value)) {
            if (change_time > options.end) break;
            if (change_time >= options.begin) candidate_times.insert(change_time);
        }
    }
    bool have_prev = false;
    bool prev_known = false;
    bool prev_one = false;
    for (const auto& change_time : candidate_times) {
        std::map<std::string, StreamValue> clock_values;
        if (!stream_collect_signal_values(file, clock_deps, change_time, clock_values, error)) return false;
        StreamValue clock_value;
        std::string eval_error;
        if (!compiled.clock.evaluate(clock_values, clock_value, eval_error)) {
            error = "clock evaluate failed: " + eval_error;
            return false;
        }
        bool cur_known = false;
        bool cur_one = false;
        cur_known = clock_value.known;
        cur_one = stream_value_truthy(clock_value, false);
        bool edge_match = false;
        if (have_prev && prev_known && cur_known) {
            edge_match = config.posedge ? (!prev_one && cur_one) : (prev_one && !cur_one);
        }
        if (edge_match) edge_times.push_back(change_time);
        prev_known = cur_known;
        prev_one = cur_one;
        have_prev = true;
    }

    bool in_packet = false;
    StreamPacket current_packet;
    int packet_index = 0;
    StreamStallWindow current_stall;
    bool in_stall = false;

    auto finish_stall = [&](npiFsdbTime end_time, int end_cycle) {
        if (!in_stall) return;
        current_stall.end_time = end_time;
        current_stall.end_cycle = end_cycle;
        analysis.stalls.push_back(current_stall);
        in_stall = false;
    };

    auto finish_packet = [&](bool partial_end) {
        if (!in_packet) return;
        current_packet.partial_end = partial_end;
        analysis.packets.push_back(current_packet);
        analysis.packet_count++;
        in_packet = false;
    };

    int cycle = 0;
    for (size_t edge_index = 0; edge_index < edge_times.size(); ++edge_index) {
        npiFsdbTime t = edge_times[edge_index];
        analysis.clock_edges++;
        std::map<std::string, StreamValue> values;
        if (!stream_collect_signal_values(file, deps, t, values, error)) return false;

        auto eval = [&](StreamExpression& expr, const std::string& label) -> StreamValue {
            if (expr.text().empty()) return StreamValue{"0", true};
            StreamValue out;
            std::string eval_error;
            if (!expr.evaluate(values, out, eval_error)) {
                error = label + " evaluate failed: " + eval_error;
                return x_value();
            }
            return out;
        };

        StreamValue reset_v = config.reset.empty() ? StreamValue{"0", true} : eval(compiled.reset, "reset");
        if (!error.empty()) return false;
        StreamValue vld_v = eval(compiled.vld, "vld");
        if (!error.empty()) return false;
        StreamValue rdy_v = config.rdy.empty() ? StreamValue{"1", true} : eval(compiled.rdy, "rdy");
        if (!error.empty()) return false;
        StreamValue bp_v = config.bp.empty() ? StreamValue{"0", true} : eval(compiled.bp, "bp");
        if (!error.empty()) return false;
        StreamValue sop_v = config.sop.empty() ? StreamValue{"0", true} : eval(compiled.sop, "sop");
        if (!error.empty()) return false;
        StreamValue eop_v = config.eop.empty() ? StreamValue{"0", true} : eval(compiled.eop, "eop");
        if (!error.empty()) return false;

        StreamRow row;
        row.cycle = cycle;
        row.time = t;
        row.reset = stream_value_truthy(reset_v, true);
        row.vld = stream_value_truthy(vld_v, false);
        row.rdy = stream_value_truthy(rdy_v, false);
        row.bp = stream_value_truthy(bp_v, true);
        row.sop = stream_value_truthy(sop_v, false);
        row.eop = stream_value_truthy(eop_v, false);
        row.control_xz_count = (stream_value_has_xz(reset_v) ? 1 : 0) +
                               (stream_value_has_xz(vld_v) ? 1 : 0) +
                               (stream_value_has_xz(rdy_v) ? 1 : 0) +
                               (stream_value_has_xz(bp_v) ? 1 : 0) +
                               (stream_value_has_xz(sop_v) ? 1 : 0) +
                               (stream_value_has_xz(eop_v) ? 1 : 0);

        bool flow = true;
        if (!config.rdy.empty()) flow = flow && row.rdy;
        if (!config.bp.empty()) flow = flow && !row.bp;
        row.transfer = row.vld && flow && !row.reset;
        if (!config.rdy.empty() && !config.bp.empty()) {
            row.stall = row.vld && (!row.rdy || row.bp);
            if (!row.rdy && row.bp) row.stall_reason = "rdy_low_and_bp_high";
            else if (!row.rdy) row.stall_reason = "rdy_low";
            else if (row.bp) row.stall_reason = "bp_high";
        } else if (!config.rdy.empty()) {
            row.stall = row.vld && !row.rdy;
            if (row.stall) row.stall_reason = "rdy_low";
        } else if (!config.bp.empty()) {
            row.stall = row.vld && row.bp;
            if (row.stall) row.stall_reason = "bp_high";
        }
        if (row.vld) analysis.vld_cycles++;
        if (row.stall) analysis.stall_cycles++;
        if (row.vld && row.rdy && row.bp && !config.rdy.empty() && !config.bp.empty())
            analysis.ready_bp_conflict_count++;
        analysis.control_xz_count += row.control_xz_count;

        if (!config.data.empty()) {
            row.fields["data"] = eval(compiled.data, "data");
            if (!error.empty()) return false;
        }
        for (auto& kv : compiled.data_fields) {
            row.fields[kv.first] = eval(kv.second, "data_fields." + kv.first);
            if (!error.empty()) return false;
        }
        if (!config.channel_id.empty()) {
            row.channel = eval(compiled.channel, "channel_id");
            if (!error.empty()) return false;
        }
        for (const auto& kv : row.fields) {
            if (stream_value_has_xz(kv.second)) row.data_xz_count++;
        }
        analysis.data_xz_count += row.data_xz_count;

        bool channel_ok = options.channel_filter.empty();
        if (!options.channel_filter.empty()) {
            bool ok = false;
            unsigned long long want = parse_u64(options.channel_filter, ok);
            unsigned long long got = 0;
            channel_ok = ok && value_u64(row.channel, got) && got == want;
        }

        if (row.stall) {
            if (!in_stall) {
                in_stall = true;
                current_stall = StreamStallWindow();
                current_stall.start_cycle = cycle;
                current_stall.start_time = t;
                current_stall.reason = row.stall_reason;
            }
            current_stall.cycles++;
        } else {
            finish_stall(t, cycle);
        }

        if (row.transfer) {
            analysis.transfer_count++;
            if (stream_packet_enabled(config)) {
                if (row.sop) {
                    if (in_packet) finish_packet(true);
                    in_packet = true;
                    current_packet = StreamPacket();
                    current_packet.packet_index = packet_index++;
                    current_packet.start_cycle = cycle;
                    current_packet.end_cycle = cycle;
                    current_packet.start_time = t;
                    current_packet.end_time = t;
                    current_packet.first_fields = row.fields;
                } else if (!in_packet && row.eop) {
                    current_packet = StreamPacket();
                    current_packet.packet_index = packet_index++;
                    current_packet.start_cycle = cycle;
                    current_packet.start_time = t;
                    current_packet.partial_begin = true;
                    in_packet = true;
                }
                if (in_packet) {
                    row.packet_index = current_packet.packet_index;
                    row.beat_index = current_packet.beat_count;
                    current_packet.end_cycle = cycle;
                    current_packet.end_time = t;
                    current_packet.beat_count++;
                    current_packet.last_fields = row.fields;
                    if (row.eop) finish_packet(false);
                }
            }
            if (channel_ok) {
                analysis.transfers.push_back(row);
                if (options.limit > 0 && static_cast<int>(analysis.transfers.size()) > options.limit) {
                    analysis.truncated = true;
                }
            }
        }

        cycle++;
    }
    finish_stall(edge_times.empty() ? options.end : edge_times.back(), cycle);
    finish_packet(true);
    return true;
}

bool StreamAnalyzer::match_row(const StreamRow& row, const StreamMatch& match, std::string& error) const {
    auto it = row.fields.find(match.field);
    if (it == row.fields.end()) {
        error = "match field not found: " + match.field;
        return false;
    }
    unsigned long long lhs = 0;
    if (!value_u64(it->second, lhs)) return false;
    bool ok = false;
    unsigned long long value = parse_u64(match.value, ok);
    if (!ok && match.op != "in_range") {
        error = "invalid match value: " + match.value;
        return false;
    }
    if (match.op == "==") return lhs == value;
    if (match.op == "!=") return lhs != value;
    if (match.op == ">") return lhs > value;
    if (match.op == ">=") return lhs >= value;
    if (match.op == "<") return lhs < value;
    if (match.op == "<=") return lhs <= value;
    if (match.op == "mask_eq") {
        bool mok = false;
        unsigned long long mask = parse_u64(match.mask, mok);
        if (!mok) {
            error = "invalid match mask: " + match.mask;
            return false;
        }
        return (lhs & mask) == (value & mask);
    }
    if (match.op == "in_range") {
        bool lok = false, hik = false;
        unsigned long long lo = parse_u64(match.lo, lok);
        unsigned long long hi = parse_u64(match.hi, hik);
        if (!lok || !hik) {
            error = "invalid in_range bounds";
            return false;
        }
        return lhs >= lo && lhs <= hi;
    }
    error = "unsupported match op: " + match.op;
    return false;
}

Json stream_row_json(const StreamRow& row) {
    Json j;
    j["cycle"] = row.cycle;
    j["time"] = format_time(row.time);
    j["vld"] = row.vld;
    j["rdy"] = row.rdy;
    j["bp"] = row.bp;
    j["sop"] = row.sop;
    j["eop"] = row.eop;
    j["transfer"] = row.transfer;
    j["stall"] = row.stall;
    if (!row.stall_reason.empty()) j["stall_reason"] = row.stall_reason;
    if (row.packet_index >= 0) j["packet_index"] = row.packet_index;
    j["beat_index"] = row.beat_index;
    j["fields"] = fields_json(row.fields);
    if (!row.channel.bits.empty()) j["channel_id"] = stream_value_json(row.channel);
    return j;
}

Json stream_stall_json(const StreamStallWindow& stall) {
    return Json{{"start_cycle", stall.start_cycle}, {"end_cycle", stall.end_cycle},
                {"start_time", format_time(stall.start_time)}, {"end_time", format_time(stall.end_time)},
                {"cycles", stall.cycles}, {"reason", stall.reason}};
}

Json stream_packet_json(const StreamPacket& packet) {
    return Json{{"packet_index", packet.packet_index},
                {"start_cycle", packet.start_cycle}, {"end_cycle", packet.end_cycle},
                {"start_time", format_time(packet.start_time)}, {"end_time", format_time(packet.end_time)},
                {"beat_count", packet.beat_count},
                {"partial_begin", packet.partial_begin}, {"partial_end", packet.partial_end},
                {"first_fields", fields_json(packet.first_fields)},
                {"last_fields", fields_json(packet.last_fields)}};
}

Json stream_summary_json(const StreamConfig& config, const StreamAnalysis& analysis) {
    Json j;
    j["stream"] = config.name;
    j["clock"] = config.clock;
    j["clock_edge"] = config.posedge ? "posedge" : "negedge";
    j["handshake"] = stream_handshake_text(config);
    j["packet_enabled"] = stream_packet_enabled(config);
    j["clock_edges"] = analysis.clock_edges;
    j["vld_cycles"] = analysis.vld_cycles;
    j["transfer_count"] = analysis.transfer_count;
    j["stall_cycles"] = analysis.stall_cycles;
    j["stall_windows"] = analysis.stalls.size();
    j["packet_count"] = analysis.packet_count;
    j["control_xz_count"] = analysis.control_xz_count;
    j["data_xz_count"] = analysis.data_xz_count;
    j["ready_bp_conflict_count"] = analysis.ready_bp_conflict_count;
    j["truncated"] = analysis.truncated;
    if (!analysis.transfers.empty()) {
        j["first_transfer"] = stream_row_json(analysis.transfers.front());
        j["last_transfer"] = stream_row_json(analysis.transfers.back());
    }
    if (!analysis.stalls.empty()) {
        j["first_stall"] = stream_stall_json(analysis.stalls.front());
        j["last_stall"] = stream_stall_json(analysis.stalls.back());
    }
    return j;
}

} // namespace xdebug_waveform
