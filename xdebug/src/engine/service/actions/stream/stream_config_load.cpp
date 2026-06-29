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

class StreamConfigLoadHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "stream.config.load"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& request, EngineActionContext& ctx) const override {
        Json args = request.value("args", Json::object());
        Json root;
        std::string error;
        if (!xdebug_waveform::load_stream_config_arg(args, root, error)) return err("INVALID_REQUEST", error);
        std::vector<StreamConfig> streams;
        if (!xdebug_waveform::parse_stream_config_list(root, streams, error)) return err("INVALID_REQUEST", error);

        StreamAnalyzer analyzer;
        Json validation = Json::array();
        for (const auto& stream : streams) {
            std::vector<xdebug_waveform::StreamValidationIssue> issues;
            std::string validate_error;
            analyzer.validate_static(g_fsdb_file, stream, issues, validate_error);
            for (const auto& issue : issues) validation.push_back(
                {{"stream", stream.name}, {"severity", issue.severity},
                 {"code", issue.code}, {"message", issue.message}});
            for (const auto& issue : issues) {
                if (issue.severity == "ERROR") return err("INVALID_REQUEST", issue.message);
            }
        }

        std::string mode = args.value("mode", std::string("replace"));
        StreamManager manager;
        if (!manager.load_configs(xdebug_waveform::g_session_id, streams, mode, error))
            return err("INVALID_REQUEST", error);
        Json out;
        out["summary"] = {{"loaded", streams.size()}, {"mode", mode}};
        out["streams"] = Json::array();
        for (const auto& stream : streams) out["streams"].push_back(stream.name);
        out["issues"] = validation;
        return out;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_stream_config_load_handler() {
    return std::unique_ptr<EngineActionHandler>(new StreamConfigLoadHandler);
}

}  // namespace xdebug_design
