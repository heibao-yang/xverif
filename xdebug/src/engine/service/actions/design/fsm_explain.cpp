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
class FsmExplainHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "fsm.explain"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return false; }
    Json run(const Json& request, EngineActionContext& ctx) const override {
        Json seq_resp = run_sequential_update_action(request, ctx);
        if (seq_resp.contains("error")) return seq_resp;

        Json args = request.value("args", Json::object());
        std::string signal = args.value("signal", "");
        nlohmann::json seq = nlohmann::json::parse(
            seq_resp.value("sequential_update", Json::object()).dump());

        nlohmann::json transitions = nlohmann::json::array();
        for (const auto& rule : seq.value("rules", nlohmann::json::array())) {
            std::string kind = rule.value("kind", "");
            if (kind == "reset" || kind == "update") {
                transitions.push_back({
                    {"from","current"}, {"to",rule.value("next_value_text","")},
                    {"condition",rule.value("condition",nlohmann::json::object())},
                    {"kind", kind == "reset" ? "reset_transition" : "transition"},
                    {"source",rule.value("source","")},
                    {"location",rule.value("location",nlohmann::json::object())}
                });
            }
        }

        Json out;
        out["summary"] = {{"signal",signal},{"transition_count",transitions.size()},
            {"confidence",seq.value("confidence","unknown")}};
        out["fsm"] = Json::parse(nlohmann::json{
            {"state_signal",signal},{"clock",seq["clock"]},
            {"reset",seq["reset"]},{"transitions",transitions},
            {"rules",seq["rules"]},{"confidence",seq.value("confidence","unknown")},
            {"confidence_reason",seq.value("confidence_reason","")}
        }.dump());
        return out;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_fsm_explain_handler() {
    return std::unique_ptr<EngineActionHandler>(new FsmExplainHandler);
}

}  // namespace xdebug_design
