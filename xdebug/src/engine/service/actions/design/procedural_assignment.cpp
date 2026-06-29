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

class ProceduralAssignmentHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "procedural.assignment"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return false; }
    Json run(const Json& request, EngineActionContext& ctx) const override {
        Json args = request.value("args", Json::object());
        std::string signal = args.value("signal", "");
        if (signal.empty()) return Json({{"error","MISSING_FIELD"},{"message","args.signal"}});

        TraceOptions opts = parse_trace_opts(args);
        nlohmann::json trace = trace_one_signal(signal, TraceMode::Driver, opts);
        nlohmann::json assignments = detail::normalize_assignments_with_conditions(trace);

        nlohmann::json defaults = nlohmann::json::array();
        nlohmann::json branches = nlohmann::json::array();
        for (const auto& a : assignments) {
            if (a.value("assignment_role", "") == "default_or_unconditional") defaults.push_back(a);
            else branches.push_back(a);
        }

        nlohmann::json enclosing;
        if (!assignments.empty())
            enclosing = nlohmann::json{{"type","procedural_or_continuous"},
                {"location",assignments[0].value("location",nlohmann::json::object())}};
        else
            enclosing = nlohmann::json{{"type","unknown"}};

        Json out;
        out["signal"] = signal;
        out["assignment_count"] = assignments.size();
        out["branch_count"] = branches.size();
        out["default_count"] = defaults.size();
        out["confidence"] = trace.value("confidence","unknown");
        nlohmann::json procedural_assignment = {
            {"target",signal},{"enclosing_block",enclosing},
            {"assignments",assignments},
            {"branch_assignments",branches},
            {"control_dependencies",trace.value("control_dependencies",nlohmann::json::array())},
            {"dependency_edges",trace.value("dependency_edges",nlohmann::json::array())},
            {"confidence",trace.value("confidence","unknown")},
            {"confidence_reason",trace.value("confidence_reason","")}
        };
        if (defaults != assignments) procedural_assignment["default_assignments"] = defaults;
        out["procedural_assignment"] = Json::parse(procedural_assignment.dump());
        return with_scalar_summary(out);
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_procedural_assignment_handler() {
    return std::unique_ptr<EngineActionHandler>(new ProceduralAssignmentHandler);
}

}  // namespace xdebug_design
