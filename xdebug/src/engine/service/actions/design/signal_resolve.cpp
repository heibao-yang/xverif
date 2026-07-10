#include "service/engine_action_handler.h"
#include "service/engine_action_registry.h"
#include "service/engine_globals.h"
#include "service/design_postprocess.h"
#include "service/trace_bfs_engine.h"
#include "design_action_helpers.h"

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

class SignalResolveHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "signal.resolve"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return false; }

    Json run(const Json& request, EngineActionContext& ctx) const override {
        Json args = request.value("args", Json::object());
        std::string signal = args.value("signal", std::string());
        if (signal.empty()) return design_missing_signal_error(action_name());
        SignalResolveResult result;
        Json failure;
        if (!resolve_design_signal(action_name(), signal, result, failure)) return failure;
        Json matches = Json::array();
        for (const auto& match : result.matches) {
            matches.push_back({{"signal", match.signal}, {"type", match.type},
                               {"file", match.file}, {"line", match.line}});
        }
        return Json{{"summary", {{"status", "found"}, {"query", signal},
                                  {"match_count", static_cast<int>(matches.size())},
                                  {"truncated", result.truncated}}},
                    {"matches", matches}};
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_signal_resolve_handler() {
    return std::unique_ptr<EngineActionHandler>(new SignalResolveHandler);
}

}  // namespace xdebug_design
