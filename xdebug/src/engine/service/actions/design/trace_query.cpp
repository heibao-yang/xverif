#include "service/engine_action_handler.h"
#include "service/engine_action_registry.h"
#include "service/engine_globals.h"
#include "service/design_postprocess.h"
#include "service/trace_bfs_engine.h"

#include "core/ai/common_blocks.h"

#include "design/trace/trace_engine.h"
#include "design/signal/signal_finder.h"
#include "design/service/action_support.h"

#include "npi.h"
#include "npi_fsdb.h"
#include "npi_L1.h"

#include <fstream>
#include <map>
#include <memory>
#include <set>

namespace xdebug_design {
namespace {

static TraceOptions parse_trace_opts(const Json& args) {
    TraceOptions opts;
    opts.limit = args.value("limit", 0);
    opts.role = args.value("role", std::string());
    opts.no_statement_only = args.value("no_statement_only", false);
    if (args.contains("include_statement_only") && args["include_statement_only"].is_boolean())
        opts.no_statement_only = !args["include_statement_only"].get<bool>();
    return opts;
}
static nlohmann::json trace_one_signal(const std::string& signal,
                                        TraceMode mode,
                                        const TraceOptions& opts) {
    TraceEngine engine;
    TraceResult r = engine.trace(signal, mode, opts);
    return nlohmann::json::parse(engine.render_ai_json(r));
}
static TraceMode trace_mode_from_direction(const std::string& dir) {
    return dir == "load" ? TraceMode::Load : TraceMode::Driver;
}

class TraceQueryHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "trace.query"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return false; }
    Json run(const Json& request, EngineActionContext& ctx) const override {
        Json args = request.value("args", Json::object());
        std::string signal = args.value("signal", "");
        if (signal.empty()) return Json({{"error","MISSING_FIELD"},{"message","args.signal"}});
        std::string mode_str = args.value("mode", "driver");
        TraceMode mode = trace_mode_from_direction(mode_str);
        TraceOptions opts = parse_trace_opts(args);
        Json out = Json::parse(trace_one_signal(signal, mode, opts).dump());
        xdebug::append_common_blocks_to_payload(out);
        return out;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_trace_query_handler() {
    return std::unique_ptr<EngineActionHandler>(new TraceQueryHandler);
}

}  // namespace xdebug_design
