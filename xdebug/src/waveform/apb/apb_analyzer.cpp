#include "apb_analyzer.h"
#include "../common/clock_sampling.h"
#include "../server/fsdb_value_reader.h"
#include "../server/fsdb_scan_utils.h"
#include "npi_fsdb.h"
#include "npi_L1.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <algorithm>

namespace xdebug_waveform {

bool ApbAnalyzer::parse_hex_value(const std::string& hex_str, uint64_t& out) {
    if (hex_str.empty()) return false;
    const char* start = hex_str.c_str();
    char* end = nullptr;
    out = strtoull(start, &end, 16);
    return end != start;
}

const ApbResult* ApbAnalyzer::get_result(const std::string& name) const {
    auto it = results_.find(name);
    if (it != results_.end()) return &it->second;
    return nullptr;
}

ApbResult* ApbAnalyzer::get_result_mut(const std::string& name) {
    auto it = results_.find(name);
    if (it != results_.end()) return &it->second;
    return nullptr;
}

ApbCursor* ApbAnalyzer::get_cursor_mut(const std::string& name) {
    auto it = cursors_.find(name);
    if (it != cursors_.end()) return &it->second;
    return nullptr;
}

bool ApbAnalyzer::analyze(const std::string& name, npiFsdbFileHandle file, const ApbConfig& config) {
    if (get_result(name) != nullptr) {
        return true; // already cached
    }

    ClockSampleSpec clock_sample = config.clock_sample;
    std::string normalize_error;
    if (!normalize_clock_sample_spec(file, clock_sample, normalize_error)) return false;

    std::vector<std::string> signals = {
        config.rst_n, config.psel, config.penable,
        config.pwrite, config.paddr, config.pwdata, config.prdata
    };
    const int pready_index = config.pready.empty()
        ? -1
        : static_cast<int>(signals.size());
    if (pready_index >= 0) signals.push_back(config.pready);
    const int pslverr_index = config.pslverr.empty()
        ? -1
        : static_cast<int>(signals.size());
    if (pslverr_index >= 0) signals.push_back(config.pslverr);
    std::vector<npiFsdbSigHandle> sig_handles;
    sig_handles.reserve(signals.size());
    for (const auto& signal : signals) {
        npiFsdbSigHandle h = npi_fsdb_sig_by_name(file, signal.c_str(), NULL);
        if (!h) return false;
        sig_handles.push_back(h);
    }
    std::vector<ClockSampleSignal> sample_signals;
    for (size_t i = 0; i < signals.size(); ++i) {
        sample_signals.push_back({signals[i], signals[i], sig_handles[i]});
    }

    ApbResult result;
    bool completion_seen = false;

    auto process_edge = [&](npiFsdbTime t, const std::vector<std::string>& values) {
        if (values.size() < 7) return;

        const std::string& rst_n_val = values[0];
        const std::string& psel_val = values[1];
        const std::string& penable_val = values[2];
        const std::string& pwrite_val = values[3];
        const std::string& paddr_val = values[4];
        const std::string& pwdata_val = values[5];
        const std::string& prdata_val = values[6];

        // Check rst_n == 1
        if (rst_n_val.empty() || rst_n_val == "0" || rst_n_val == "X" || rst_n_val == "Z") {
            completion_seen = false;
            return;
        }
        // Check psel == 1
        if (psel_val.empty() || psel_val == "0" || psel_val == "X" || psel_val == "Z") {
            completion_seen = false;
            return;
        }
        // Check penable == 1
        if (penable_val.empty() || penable_val == "0" || penable_val == "X" || penable_val == "Z") {
            completion_seen = false;
            return;
        }
        if (pready_index >= 0) {
            const std::string& pready_val = values[static_cast<size_t>(pready_index)];
            if (pready_val.empty() || pready_val == "0" ||
                pready_val == "X" || pready_val == "Z") {
                return;
            }
            if (completion_seen) return;
            completion_seen = true;
        }

        bool is_write = !(pwrite_val.empty() || pwrite_val == "0" || pwrite_val == "X" || pwrite_val == "Z");

        ApbTransaction txn;
        txn.time = t;
        txn.addr = paddr_val;
        txn.data = is_write ? pwdata_val : prdata_val;
        txn.is_write = is_write;
        if (pslverr_index >= 0) {
            const std::string& pslverr_val =
                values[static_cast<size_t>(pslverr_index)];
            txn.has_error = !(pslverr_val.empty() || pslverr_val == "0" ||
                              pslverr_val == "X" || pslverr_val == "Z");
        }

        result.all.push_back(txn);
        if (is_write) {
            result.writes.push_back(txn);
        } else {
            result.reads.push_back(txn);
        }
    };

    npiFsdbTime min_time = 0;
    npiFsdbTime max_time = 0;
    npi_fsdb_min_time(file, &min_time);
    npi_fsdb_max_time(file, &max_time);

    ClockSampleScanner scanner(file, clock_sample);
    std::string scan_error;
    int sample_count = 0;
    bool truncated = false;
    if (!scanner.scan(sample_signals, min_time, max_time, npiFsdbHexStrVal, '\0', -1,
        [&](const ClockSample& sample) -> bool {
            process_edge(sample.time, sample.values);
            return true;
        }, scan_error, sample_count, truncated)) {
        return false;
    }
    // Sort by time just in case (though VCT should naturally be in order)
    auto cmp = [](const ApbTransaction& a, const ApbTransaction& b) { return a.time < b.time; };
    std::sort(result.all.begin(), result.all.end(), cmp);
    std::sort(result.writes.begin(), result.writes.end(), cmp);
    std::sort(result.reads.begin(), result.reads.end(), cmp);

    results_[name] = std::move(result);
    cursors_[name] = ApbCursor();
    return true;
}

size_t ApbAnalyzer::get_write_count(const std::string& name) const {
    const ApbResult* r = get_result(name);
    return r ? r->writes.size() : 0;
}

size_t ApbAnalyzer::get_read_count(const std::string& name) const {
    const ApbResult* r = get_result(name);
    return r ? r->reads.size() : 0;
}

bool ApbAnalyzer::get_write_by_addr(const std::string& name, uint64_t addr, const ApbTransaction*& out) const {
    const ApbResult* r = get_result(name);
    if (!r) return false;
    for (const auto& txn : r->writes) {
        uint64_t txn_addr = 0;
        if (parse_hex_value(txn.addr, txn_addr) && txn_addr == addr) {
            out = &txn;
            return true;
        }
    }
    return false;
}

bool ApbAnalyzer::get_write_by_addr_num(const std::string& name, uint64_t addr, size_t num, const ApbTransaction*& out) const {
    const ApbResult* r = get_result(name);
    if (!r) return false;
    if (num == 0) return false;
    size_t count = 0;
    for (const auto& txn : r->writes) {
        uint64_t txn_addr = 0;
        if (parse_hex_value(txn.addr, txn_addr) && txn_addr == addr) {
            if (++count == num) {
                out = &txn;
                return true;
            }
        }
    }
    return false;
}

bool ApbAnalyzer::get_write_by_addr_last(const std::string& name, uint64_t addr, const ApbTransaction*& out) const {
    const ApbResult* r = get_result(name);
    if (!r) return false;
    const ApbTransaction* found = nullptr;
    for (const auto& txn : r->writes) {
        uint64_t txn_addr = 0;
        if (parse_hex_value(txn.addr, txn_addr) && txn_addr == addr) {
            found = &txn;
        }
    }
    if (found) {
        out = found;
        return true;
    }
    return false;
}

bool ApbAnalyzer::get_write_by_num(const std::string& name, size_t num, const ApbTransaction*& out) const {
    const ApbResult* r = get_result(name);
    if (!r || num == 0 || num > r->writes.size()) return false;
    out = &r->writes[num - 1];
    return true;
}

bool ApbAnalyzer::get_write_last(const std::string& name, const ApbTransaction*& out) const {
    const ApbResult* r = get_result(name);
    if (!r || r->writes.empty()) return false;
    out = &r->writes.back();
    return true;
}

bool ApbAnalyzer::get_read_by_addr(const std::string& name, uint64_t addr, const ApbTransaction*& out) const {
    const ApbResult* r = get_result(name);
    if (!r) return false;
    for (const auto& txn : r->reads) {
        uint64_t txn_addr = 0;
        if (parse_hex_value(txn.addr, txn_addr) && txn_addr == addr) {
            out = &txn;
            return true;
        }
    }
    return false;
}

bool ApbAnalyzer::get_read_by_addr_num(const std::string& name, uint64_t addr, size_t num, const ApbTransaction*& out) const {
    const ApbResult* r = get_result(name);
    if (!r) return false;
    if (num == 0) return false;
    size_t count = 0;
    for (const auto& txn : r->reads) {
        uint64_t txn_addr = 0;
        if (parse_hex_value(txn.addr, txn_addr) && txn_addr == addr) {
            if (++count == num) {
                out = &txn;
                return true;
            }
        }
    }
    return false;
}

bool ApbAnalyzer::get_read_by_addr_last(const std::string& name, uint64_t addr, const ApbTransaction*& out) const {
    const ApbResult* r = get_result(name);
    if (!r) return false;
    const ApbTransaction* found = nullptr;
    for (const auto& txn : r->reads) {
        uint64_t txn_addr = 0;
        if (parse_hex_value(txn.addr, txn_addr) && txn_addr == addr) {
            found = &txn;
        }
    }
    if (found) {
        out = found;
        return true;
    }
    return false;
}

bool ApbAnalyzer::get_read_by_num(const std::string& name, size_t num, const ApbTransaction*& out) const {
    const ApbResult* r = get_result(name);
    if (!r || num == 0 || num > r->reads.size()) return false;
    out = &r->reads[num - 1];
    return true;
}

bool ApbAnalyzer::get_read_last(const std::string& name, const ApbTransaction*& out) const {
    const ApbResult* r = get_result(name);
    if (!r || r->reads.empty()) return false;
    out = &r->reads.back();
    return true;
}

// Cursor operations
bool ApbAnalyzer::cursor_begin(const std::string& name, int filter, const ApbTransaction*& out) {
    ApbResult* r = get_result_mut(name);
    ApbCursor* c = get_cursor_mut(name);
    if (!r || !c) return false;

    if (filter == 1) {
        c->wr_idx = 0;
        if (c->wr_idx < r->writes.size()) {
            out = &r->writes[c->wr_idx];
            return true;
        }
    } else if (filter == 2) {
        c->rd_idx = 0;
        if (c->rd_idx < r->reads.size()) {
            out = &r->reads[c->rd_idx];
            return true;
        }
    } else {
        c->all_idx = 0;
        if (c->all_idx < r->all.size()) {
            out = &r->all[c->all_idx];
            return true;
        }
    }
    return false;
}

bool ApbAnalyzer::cursor_next(const std::string& name, int filter, const ApbTransaction*& out) {
    ApbResult* r = get_result_mut(name);
    ApbCursor* c = get_cursor_mut(name);
    if (!r || !c) return false;

    if (filter == 1) {
        if (c->wr_idx + 1 < r->writes.size()) {
            out = &r->writes[++c->wr_idx];
            return true;
        }
    } else if (filter == 2) {
        if (c->rd_idx + 1 < r->reads.size()) {
            out = &r->reads[++c->rd_idx];
            return true;
        }
    } else {
        if (c->all_idx + 1 < r->all.size()) {
            out = &r->all[++c->all_idx];
            return true;
        }
    }
    return false;
}

bool ApbAnalyzer::cursor_prev(const std::string& name, int filter, const ApbTransaction*& out) {
    ApbResult* r = get_result_mut(name);
    ApbCursor* c = get_cursor_mut(name);
    if (!r || !c) return false;

    if (filter == 1) {
        if (c->wr_idx > 0) {
            out = &r->writes[--c->wr_idx];
            return true;
        }
    } else if (filter == 2) {
        if (c->rd_idx > 0) {
            out = &r->reads[--c->rd_idx];
            return true;
        }
    } else {
        if (c->all_idx > 0) {
            out = &r->all[--c->all_idx];
            return true;
        }
    }
    return false;
}

bool ApbAnalyzer::cursor_last(const std::string& name, int filter, const ApbTransaction*& out) {
    ApbResult* r = get_result_mut(name);
    ApbCursor* c = get_cursor_mut(name);
    if (!r || !c) return false;

    if (filter == 1) {
        if (!r->writes.empty()) {
            c->wr_idx = r->writes.size() - 1;
            out = &r->writes[c->wr_idx];
            return true;
        }
    } else if (filter == 2) {
        if (!r->reads.empty()) {
            c->rd_idx = r->reads.size() - 1;
            out = &r->reads[c->rd_idx];
            return true;
        }
    } else {
        if (!r->all.empty()) {
            c->all_idx = r->all.size() - 1;
            out = &r->all[c->all_idx];
            return true;
        }
    }
    return false;
}

bool ApbAnalyzer::cursor_state(const std::string& name, int filter,
                               size_t& one_based_index, size_t& total_count) const {
    const ApbResult* result = get_result(name);
    auto cursor_it = cursors_.find(name);
    if (!result || cursor_it == cursors_.end()) return false;
    const ApbCursor& cursor = cursor_it->second;
    if (filter == 1) {
        total_count = result->writes.size();
        one_based_index = total_count == 0 ? 0 : cursor.wr_idx + 1;
    } else if (filter == 2) {
        total_count = result->reads.size();
        one_based_index = total_count == 0 ? 0 : cursor.rd_idx + 1;
    } else {
        total_count = result->all.size();
        one_based_index = total_count == 0 ? 0 : cursor.all_idx + 1;
    }
    return true;
}

bool ApbAnalyzer::get_transactions_in_range(const std::string& name,
                                            npiFsdbTime begin,
                                            npiFsdbTime end,
                                            std::vector<ApbContextTransaction>& out,
                                            int max_results) const {
    out.clear();
    const ApbResult* r = get_result(name);
    if (!r) return false;

    auto it = std::lower_bound(r->all.begin(), r->all.end(), begin,
        [](const ApbTransaction& txn, npiFsdbTime t) {
            return txn.time < t;
        });
    for (; it != r->all.end() && it->time <= end; ++it) {
        if (max_results >= 0 && static_cast<int>(out.size()) >= max_results) break;
        ApbContextTransaction item;
        item.txn = &(*it);
        out.push_back(item);
    }
    return true;
}

} // namespace xdebug_waveform
