#include "design_action_support.h"

#include "service/design_postprocess.h"
#include "service/trace_bfs_engine.h"

#include "core/ai/common_blocks.h"

#include "design/trace/trace_engine.h"
#include "design/service/action_support.h"

#include <algorithm>
#include <string>

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

static Json trace_expand_summary(const std::string& root,
                                 const std::string& direction,
                                 const detail::BfsResult& bfs,
                                 const nlohmann::json& graph,
                                 const nlohmann::json& relation_edges,
                                 int aggregated_edge_count,
                                 bool compact) {
    if (compact) {
        return {{"root_signal", root}, {"direction", direction},
                {"node_count", graph["nodes"].size()}, {"edge_count", graph["edges"].size()},
                {"truncated", bfs.truncated}};
    }
    return {{"root_signal", root}, {"direction", direction},
            {"depth", bfs.reached_depth}, {"node_count", graph["nodes"].size()},
            {"edge_count", graph["edges"].size()}, {"raw_edge_count", bfs.raw_edge_count},
            {"deduped_edge_count", bfs.all_edges.size()},
            {"duplicate_edge_count", bfs.duplicate_edge_count},
            {"relation_group_count", relation_edges.size()},
            {"aggregated_edge_count", aggregated_edge_count},
            {"failed_query_count", bfs.failed_query_count},
            {"truncated", bfs.truncated}};
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

} // namespace

Json run_trace_expand_action(const Json& request, EngineActionContext& ctx) {
    Json args = request.value("args", Json::object());
    std::string root = args.value("root_signal", args.value("signal", ""));
    std::string direction = args.value("direction", "driver");
    if (root.empty()) return Json({{"error","MISSING_FIELD"},{"message","args.signal"}});
    Json limits = request.value("limits", args.value("limits", Json::object()));

    detail::BfsOptions bopts;
    bopts.root = root;
    bopts.direction = direction;
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
    int agg_count = 0;
    int max_ev = std::max(1, limits.value("max_evidence_per_edge", 3));
    nlohmann::json rel_edges = detail::aggregate_edges_by_relation(bfs.all_edges, max_ev, agg_count);
    nlohmann::json trace;
    trace["query"] = root; trace["mode"] = direction;
    trace["dependency_edges"] = rel_edges;
    trace["confidence"] = bfs.first_confidence;
    trace["truncated"] = bfs.truncated;
    nlohmann::json graph = detail::graph_from_trace(trace, root);

    Json out;
    bool compact = compact_mode(request) && !include_arg(request, "include_debug");
    Json summary = trace_expand_summary(root, direction, bfs, graph, rel_edges, agg_count, compact);
    out["summary"] = summary;
    for (auto it = summary.begin(); it != summary.end(); ++it) {
        if (it.value().is_string() || it.value().is_number() || it.value().is_boolean())
            out[it.key()] = it.value();
    }
    out["meta"] = {{"truncated", bfs.truncated}};
    out["truncated"] = bfs.truncated;
    out["graph"] = Json::parse(graph.dump());
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

Json run_sequential_update_action(const Json& request, EngineActionContext& ctx) {
    Json args = request.value("args", Json::object());
    std::string signal = args.value("signal", "");
    if (signal.empty()) return Json({{"error","MISSING_FIELD"},{"message","args.signal"}});

    TraceOptions opts = parse_trace_opts(args);
    nlohmann::json trace = trace_one_signal(signal, TraceMode::Driver, opts);
    nlohmann::json assignments = detail::normalize_assignments_with_conditions(trace);
    nlohmann::json controls = trace.value("control_dependencies", nlohmann::json::array());
    nlohmann::json timing = assignments.empty()
        ? nlohmann::json{{"clock",nullptr},{"reset",nullptr},{"event_controls",nlohmann::json::array()}}
        : detail::infer_clock_reset_from_assignment(assignments[0], controls);

    nlohmann::json rules = nlohmann::json::array();
    for (const auto& assignment : assignments) {
        nlohmann::json conditions = assignment.value("active_conditions", nlohmann::json::array());
        if (conditions.empty()) conditions.push_back({{"text",""},{"ast",nlohmann::json::object()},{"signals",nlohmann::json::array()}});
        for (const auto& cond : conditions) {
            rules.push_back({
                {"kind", detail::classify_update_rule(assignment, cond, signal)},
                {"condition", cond},
                {"next_value", assignment.value("rhs", nlohmann::json::object())},
                {"next_value_text", assignment.value("rhs", nlohmann::json::object()).value("text", assignment.value("source", ""))},
                {"rhs_signals", assignment.value("rhs_signals", nlohmann::json::array())},
                {"source", assignment.value("source", "")},
                {"location", assignment.value("location", nlohmann::json::object())}
            });
        }
    }

    Json out;
    out["summary"] = {{"signal",signal},{"rule_count",rules.size()},
        {"clock", Json::parse(timing["clock"].dump())},
        {"reset", Json::parse(timing["reset"].dump())},
        {"confidence",trace.value("confidence","unknown")}};
    out["sequential_update"] = Json::parse(nlohmann::json{
        {"target",signal},
        {"clock", timing["clock"]}, {"reset", timing["reset"]},
        {"event_controls", timing["event_controls"]},
        {"rules", rules}, {"confidence", trace.value("confidence","unknown")},
        {"confidence_reason", trace.value("confidence_reason","")}
    }.dump());
    return out;
}

} // namespace xdebug_design
