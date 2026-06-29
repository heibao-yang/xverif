#include "service/engine_action_handler.h"
#include "service/engine_action_registry.h"
#include "service/engine_globals.h"

#include "waveform/common/xdebug_waveform_paths.h"
#include "waveform/stream/stream_analyzer.h"
#include "waveform/stream/stream_exporter.h"
#include "waveform/stream/stream_manager.h"
#include "core/npi/time_contract.h"

#include "npi_fsdb.h"
#include "npi_L1.h"

#include <ctime>
#include <memory>
#include <sstream>
#include <sys/stat.h>

namespace xdebug_design {
namespace {

using xdebug_waveform::Json;
using xdebug_waveform::StreamAnalysis;
using xdebug_waveform::StreamAnalyzer;
using xdebug_waveform::StreamConfig;
using xdebug_waveform::StreamExporter;
using xdebug_waveform::StreamManager;
using xdebug_waveform::StreamMatch;
using xdebug_waveform::StreamQueryOptions;

Json err(const std::string& code, const std::string& message) {
    return Json{{"error", code}, {"message", message}};
}
Json issue_json(const std::vector<xdebug_waveform::StreamValidationIssue>& issues) {
    Json arr = Json::array();
    for (const auto& issue : issues) {
        arr.push_back({{"severity", issue.severity}, {"code", issue.code}, {"message", issue.message}});
    }
    return arr;
}
bool get_config(const Json& args, StreamConfig& config, Json& fail) {
    std::string name = args.value("stream", args.value("name", std::string()));
    if (name.empty()) {
        fail = err("MISSING_FIELD", "args.stream is required");
        return false;
    }
    StreamManager manager;
    if (!manager.get_stream(xdebug_waveform::g_session_id, name, config)) {
        fail = err("STREAM_NOT_FOUND", "stream config not found: " + name);
        return false;
    }
    return true;
}

class StreamShowHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "stream.show"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& request, EngineActionContext& ctx) const override {
        Json fail;
        StreamConfig config;
        if (!get_config(request.value("args", Json::object()), config, fail)) return fail;
        StreamAnalyzer analyzer;
        std::vector<xdebug_waveform::StreamValidationIssue> issues;
        std::string error;
        analyzer.validate_static(g_fsdb_file, config, issues, error);
        Json out;
        out["summary"] = {{"stream", config.name}, {"handshake", xdebug_waveform::stream_handshake_text(config)},
                          {"packet_enabled", xdebug_waveform::stream_packet_enabled(config)}};
        out["config"] = xdebug_waveform::stream_config_json(config);
        out["issues"] = issue_json(issues);
        out["semantics"] = {{"transfer", xdebug_waveform::stream_handshake_text(config)},
                            {"stall", config.rdy.empty() && config.bp.empty() ? "none" : "enabled"}};
        return out;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_stream_show_handler() {
    return std::unique_ptr<EngineActionHandler>(new StreamShowHandler);
}

}  // namespace xdebug_design
