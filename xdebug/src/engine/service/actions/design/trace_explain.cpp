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
static bool compact_mode(const Json& request) {
    return request.value("output", Json::object()).value("verbosity", "") == "compact";
}
static bool include_arg(const Json& request, const char* name) {
    return request.value("args", Json::object()).value(name, false);
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
static void append_bfs_warnings(Json& out, const detail::BfsResult& bfs) {
    if (bfs.warnings.empty()) return;
    out["warnings"] = Json::array();
    for (const auto& warning : bfs.warnings) {
        try {
            out["warnings"].push_back(Json::parse(warning));
        } catch (...) {
            out["warnings"].push_back(warning);
        }
    }
}

class TraceExplainHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "trace.explain"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return false; }
    Json run(const Json& request, EngineActionContext& ctx) const override {
        Json args = request.value("args", Json::object());
        std::string root = args.value("root_signal", args.value("signal", ""));
        std::string direction = args.value("direction", "driver");
        if (root.empty()) return Json({{"error","MISSING_FIELD"},{"message","args.signal"}});
        Json limits = request.value("limits", args.value("limits", Json::object()));

        detail::BfsOptions bopts;
        bopts.root = root; bopts.direction = direction;
        bopts.max_depth = std::max(1, limits.value("max_depth", 1));
        bopts.max_nodes = std::max(1, limits.value("max_nodes", 100));
        bopts.max_edges = std::max(1, limits.value("max_edges", limits.value("max_results", 200)));
        bopts.edge_type_filter = args;

        TraceMode mode = trace_mode_from_direction(direction);
        TraceOptions topts = parse_trace_opts(args);
        auto trace_fn = [&](const std::string& signal) -> nlohmann::json {
            return trace_one_signal(signal, mode, topts);
        };

        detail::BfsResult bfs = detail::run_trace_bfs(bopts, trace_fn);
        int max_ev = std::max(1, limits.value("max_evidence_per_edge", 3));
        int agg_count = 0;
        nlohmann::json rel_edges = detail::aggregate_edges_by_relation(bfs.all_edges, max_ev, agg_count);

        nlohmann::json explanations = nlohmann::json::array();
        int skipped = 0;
        for (const auto& e : rel_edges) {
            nlohmann::json expl = detail::explanation_from_edge(e, root, direction, skipped);
            if (!expl.is_null()) explanations.push_back(expl);
        }

        nlohmann::json trace;
        trace["query"] = root;
        trace["mode"] = direction;
        trace["dependency_edges"] = rel_edges;
        trace["confidence"] = bfs.first_confidence;
        trace["truncated"] = bfs.truncated;

        Json out;
        bool compact = compact_mode(request);
        out["root_signal"] = root;
        out["direction"] = direction;
        out["explanation_count"] = explanations.size();
        out["edge_count"] = rel_edges.size();
        out["skipped_empty_dependency_count"] = skipped;
        out["truncated"] = bfs.truncated;
        out["explanations"] = Json::parse(explanations.dump());
        if (!compact || include_arg(request, "include_trace"))
            out["trace"] = Json::parse(trace.dump());
        if (!compact || include_arg(request, "include_expanded_queries"))
            out["expanded_queries"] = Json::parse(bfs.expanded_queries.dump());
        append_bfs_warnings(out, bfs);
        if (!bfs.root_error.empty()) out["error"] = Json::parse(bfs.root_error.dump());
        out = with_scalar_summary(out);
        xdebug::append_common_blocks_to_payload(out);
        return out;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_trace_explain_handler() {
    return std::unique_ptr<EngineActionHandler>(new TraceExplainHandler);
}

}  // namespace xdebug_design
