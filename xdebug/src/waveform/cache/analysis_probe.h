#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace xdebug_waveform {

struct AnalysisProbeMetrics {
    AnalysisProbeMetrics(std::size_t entries = 0,
                         std::size_t indexes = 0,
                         std::size_t resident = 0,
                         std::size_t building = 0,
                         std::uint64_t scanners = 0)
        : entry_count(entries),
          index_count(indexes),
          resident_bytes(resident),
          build_bytes(building),
          scanner_delta(scanners) {}

    std::size_t entry_count = 0;
    std::size_t index_count = 0;
    std::size_t resident_bytes = 0;
    std::size_t build_bytes = 0;
    std::uint64_t scanner_delta = 0;
};

class AnalysisProbe {
public:
    explicit AnalysisProbe(std::string path = std::string());

    bool enabled() const;
    void record(const std::string& event,
                const std::string& protocol,
                const std::string& key_material,
                const AnalysisProbeMetrics& metrics = AnalysisProbeMetrics());

private:
    std::string path_;
    std::uint64_t scanner_invocations_ = 0;
    std::uint64_t hits_ = 0;
    std::uint64_t misses_ = 0;
    std::uint64_t evictions_ = 0;
    std::uint64_t access_sequence_ = 0;
};

// Test-only internal probe. It remains disabled unless the engine process is
// started with XDEBUG_TEST_ANALYSIS_PROBE_PATH. It is intentionally not
// exposed through any public action, schema, MCP tool, JSON response, or XOUT.
AnalysisProbe& analysis_probe();

}  // namespace xdebug_waveform
