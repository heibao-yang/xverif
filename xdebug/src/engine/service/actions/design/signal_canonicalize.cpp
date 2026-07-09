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
class SignalCanonicalizeHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "signal.canonicalize"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return false; }
    Json run(const Json& request, EngineActionContext& ctx) const override {
        Json args = request.value("args", Json::object());
        std::string query = args.value("signal", "");
        if (query.empty()) return make_handler_error(
            "MISSING_FIELD",
            "args.signal is required",
            {{"invalid_arg", "args.signal"},
             {"expected", "signal path or signal-like query"},
             {"correct_example", {{"api_version", "xdebug.v1"},
                                  {"action", "signal.canonicalize"},
                                  {"args", {{"signal", "top.u.valid"}}}}}});

        SignalFinder finder;
        SignalResolveResult result = finder.resolve(query);
        nlohmann::json resolved = nlohmann::json::parse(finder.render_json(result));

        // Extract canonical from first match
        nlohmann::json canonical = nullptr, rtl_path = nullptr, leaf = nullptr, scope = nullptr;
        nlohmann::json base_signal = nullptr, select = nullptr;
        nlohmann::json aliases = nlohmann::json::array();
        bool ambiguous = false;
        nlohmann::json fsdb_candidates = nlohmann::json::array();
        nlohmann::json port_mappings = nlohmann::json::array();

        if (resolved.contains("rtl_path")) rtl_path = resolved["rtl_path"];
        if (resolved.contains("canonical_signal")) canonical = resolved["canonical_signal"];
        else if (resolved.contains("canonical")) canonical = resolved["canonical"];
        else if (rtl_path.is_string()) canonical = rtl_path;
        else canonical = query;

        if (resolved.contains("leaf")) leaf = resolved["leaf"];
        if (resolved.contains("scope")) scope = resolved["scope"];
        if (resolved.contains("base_signal")) base_signal = resolved["base_signal"];
        if (resolved.contains("select")) select = resolved["select"];
        if (resolved.contains("aliases")) aliases = resolved["aliases"];
        if (resolved.contains("ambiguous")) ambiguous = resolved["ambiguous"].get<bool>();
        if (resolved.contains("fsdb_candidates")) fsdb_candidates = resolved["fsdb_candidates"];
        if (resolved.contains("port_mappings")) port_mappings = resolved["port_mappings"];

        Json out;
        out["summary"] = {{"query",query},{"ambiguous",ambiguous}};
        out["query"] = query;
        out["canonical"] = Json::parse(canonical.dump());
        out["rtl_path"] = Json::parse(rtl_path.dump());
        out["leaf"] = Json::parse(leaf.dump());
        out["scope"] = Json::parse(scope.dump());
        out["base_signal"] = Json::parse(base_signal.dump());
        out["select"] = Json::parse(select.dump());
        out["ambiguous"] = ambiguous;
        out["aliases"] = Json::parse(aliases.dump());
        out["fsdb_candidates"] = Json::parse(fsdb_candidates.dump());
        out["port_mappings"] = Json::parse(port_mappings.dump());
        return out;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_signal_canonicalize_handler() {
    return std::unique_ptr<EngineActionHandler>(new SignalCanonicalizeHandler);
}

}  // namespace xdebug_design
