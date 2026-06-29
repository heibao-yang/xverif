#include "service/engine_action_handler.h"
#include "service/engine_action_registry.h"
#include "service/engine_globals.h"
#include "service/design_postprocess.h"
#include "service/trace_bfs_engine.h"
#include "design_action_support.h"

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
class TraceExpandHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "trace.expand"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return false; }
    Json run(const Json& request, EngineActionContext& ctx) const override {
        return run_trace_expand_action(request, ctx);
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_trace_expand_handler() {
    return std::unique_ptr<EngineActionHandler>(new TraceExpandHandler);
}

}  // namespace xdebug_design
