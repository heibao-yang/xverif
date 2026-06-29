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

static Json with_scalar_summary(Json out) {
    if (out.contains("summary") && out["summary"].is_object() && !out["summary"].empty())
        return out;
    Json summary = Json::object();
    for (auto it = out.begin(); it != out.end(); ++it) {
        if (it->is_string() || it->is_number() || it->is_boolean() || it->is_null())
            summary[it.key()] = it.value();
    }
    if (!summary.empty()) out["summary"] = summary;
    return out;
}

class SignalResolveHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "signal.resolve"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return false; }

    Json run(const Json& request, EngineActionContext& ctx) const override {
        Json args = request.value("args", Json::object());
        std::string signal = args.value("signal", std::string());
        if (signal.empty()) return err("MISSING_FIELD", "args.signal is required");
        SignalFinder finder;
        SignalResolveResult result = finder.resolve(signal);
        return with_scalar_summary(Json::parse(finder.render_json(result)));
    }

private:
    static Json err(const char* code, const std::string& msg) {
        Json e; e["error"] = code; e["message"] = msg; return e;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_signal_resolve_handler() {
    return std::unique_ptr<EngineActionHandler>(new SignalResolveHandler);
}

}  // namespace xdebug_design
