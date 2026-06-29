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

class TracePathHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "trace.path"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return false; }
    Json run(const Json& request, EngineActionContext& ctx) const override {
        Json args = request.value("args", Json::object());
        std::string from_sig = args.value("from_signal", "");
        std::string to_sig = args.value("to_signal", "");
        if (from_sig.empty() || to_sig.empty())
            return Json({{"error","MISSING_FIELD"},{"message","args.from_signal and args.to_signal"}});
        Json limits = request.value("limits", args.value("limits", Json::object()));

        // BFS from to_signal to find reachable signals
        detail::BfsOptions bopts;
        bopts.root = to_sig; bopts.direction = "driver";
        bopts.max_depth = std::max(1, limits.value("max_depth", 5));
        bopts.max_nodes = std::max(1, limits.value("max_nodes", 200));
        bopts.max_edges = std::max(1, limits.value("max_edges", 500));
        bopts.edge_type_filter = args;

        TraceOptions topts = parse_trace_opts(args);
        auto trace_fn = [&](const std::string& signal) -> nlohmann::json {
            return trace_one_signal(signal, TraceMode::Driver, topts);
        };

        detail::BfsResult bfs = detail::run_trace_bfs(bopts, trace_fn);

        // BFS from from_signal following edges to find paths to to_sig
        // Build adjacency: signal → [edge to next signal]
        std::map<std::string, std::vector<nlohmann::json>> adj;
        for (const auto& e : bfs.all_edges) {
            std::string e_from = e.value("from", "");
            std::string e_to = e.value("to", "");
            adj[e_from].push_back(e);
        }

        bool found = false;
        nlohmann::json paths = nlohmann::json::array();
        int max_paths = std::max(1, limits.value("max_paths", 10));
        // Simple BFS path finding
        std::vector<std::pair<std::string, nlohmann::json>> pqueue;
        pqueue.push_back({from_sig, nlohmann::json::array()});
        std::set<std::string> pvisited;
        for (size_t pi = 0; pi < pqueue.size() && (int)paths.size() < max_paths; ++pi) {
            std::string cur = pqueue[pi].first;
            nlohmann::json cur_path = pqueue[pi].second;
            if (cur == to_sig) {
                found = true;
                paths.push_back(cur_path);
                continue;
            }
            if (pvisited.count(cur)) continue;
            pvisited.insert(cur);
            auto it = adj.find(cur);
            if (it == adj.end()) continue;
            for (auto& e : it->second) {
                std::string next = e.value("to", e.value("from", ""));
                if (next == cur || next.empty()) continue;
                nlohmann::json new_path = cur_path;
                new_path.push_back(e);
                pqueue.push_back({next, new_path});
            }
        }

        Json out;
        out["from_signal"] = from_sig;
        out["to_signal"] = to_sig;
        out["found"] = found;
        out["path_count"] = paths.size();
        out["truncated"] = bfs.truncated;
        out["summary"] = {
            {"from_signal", from_sig},
            {"to_signal", to_sig},
            {"found", found},
            {"path_count", paths.size()},
            {"truncated", bfs.truncated}
        };
        out["paths"] = Json::parse(paths.dump());
        xdebug::append_common_blocks_to_payload(out);
        return out;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_trace_path_handler() {
    return std::unique_ptr<EngineActionHandler>(new TracePathHandler);
}

}  // namespace xdebug_design
