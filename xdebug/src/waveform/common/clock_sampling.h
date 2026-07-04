#pragma once

#include "npi_fsdb.h"

#include <functional>
#include <string>

namespace xdebug_waveform {

enum class ClockEdgeKind {
    Posedge,
    Negedge,
    Dual
};

enum class ClockSamplePointKind {
    Before,
    After
};

struct ClockSampleSpec {
    std::string clock;
    ClockEdgeKind edge = ClockEdgeKind::Negedge;
    ClockSamplePointKind sample_point = ClockSamplePointKind::Before;
    bool has_sample_point = false;
};

struct ClockSamplePoint {
    npiFsdbTime edge_time = 0;
    npiFsdbTime sample_time = 0;
    ClockEdgeKind edge_kind = ClockEdgeKind::Negedge;
};

const char* clock_edge_kind_text(ClockEdgeKind edge);
const char* clock_sample_point_text(ClockSamplePointKind point);
bool parse_clock_edge_kind(const std::string& text, ClockEdgeKind& edge, std::string& error);
bool parse_clock_sample_point_kind(const std::string& text,
                                   ClockSamplePointKind& point,
                                   std::string& error);
bool clock_edge_transition_matches(ClockEdgeKind requested,
                                   bool old_one,
                                   bool new_one,
                                   ClockEdgeKind* actual = nullptr);
bool normalize_clock_sample_spec(npiFsdbFileHandle fsdb,
                                 ClockSampleSpec& spec,
                                 std::string& error);

class ClockSampleTimeResolver {
public:
    ClockSampleTimeResolver(npiFsdbFileHandle fsdb, const ClockSampleSpec& spec);

    bool valid(std::string& error) const;
    bool is_clock_edge_at(npiFsdbTime time, ClockEdgeKind* edge_kind = nullptr) const;
    bool is_target_edge_at(npiFsdbTime time, ClockEdgeKind* edge_kind = nullptr) const;
    bool find_previous_sample(npiFsdbTime anchor_time,
                              ClockSamplePoint& point,
                              std::string& error) const;
    bool find_next_sample(npiFsdbTime anchor_time,
                          ClockSamplePoint& point,
                          std::string& error) const;
    bool for_each_sample_time(npiFsdbTime begin,
                              npiFsdbTime end,
                              const std::function<bool(const ClockSamplePoint&)>& callback,
                              std::string& error) const;

private:
    bool previous_single_edge_sample(ClockEdgeKind edge,
                                     npiFsdbTime anchor_time,
                                     ClockSamplePoint& point,
                                     std::string& error) const;
    bool next_single_edge_sample(ClockEdgeKind edge,
                                 npiFsdbTime anchor_time,
                                 ClockSamplePoint& point,
                                 std::string& error) const;

    npiFsdbFileHandle fsdb_ = nullptr;
    ClockSampleSpec spec_;
    npiFsdbSigHandle clock_handle_ = nullptr;
};

} // namespace xdebug_waveform
