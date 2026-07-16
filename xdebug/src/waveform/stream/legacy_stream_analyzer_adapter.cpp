#include "waveform/stream/legacy_stream_analyzer_adapter.h"

namespace xdebug_waveform {

bool LegacyStreamAnalyzerAdapter::analyze(npiFsdbFileHandle file,
                                          const StreamConfig& config,
                                          const StreamQueryOptions& options,
                                          StreamAnalysis& analysis,
                                          std::string& error) {
    return analyzer_.analyze(file, config, options, analysis, error);
}

}  // namespace xdebug_waveform
