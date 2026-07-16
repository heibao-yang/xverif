#pragma once

#include "waveform/stream/stream_analyzer.h"

namespace xdebug_waveform {

// Stable seam for differential tests while the columnar stream implementation
// is introduced. Phase 0 deliberately delegates to the existing analyzer so
// public behavior remains unchanged; later phases keep this path as the oracle.
class LegacyStreamAnalyzerAdapter {
public:
    bool analyze(npiFsdbFileHandle file, const StreamConfig& config,
                 const StreamQueryOptions& options, StreamAnalysis& analysis,
                 std::string& error);

private:
    StreamAnalyzer analyzer_;
};

}  // namespace xdebug_waveform
