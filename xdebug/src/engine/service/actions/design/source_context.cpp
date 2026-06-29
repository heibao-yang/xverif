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
class SourceContextHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "source.context"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return false; }
    Json run(const Json& request, EngineActionContext& ctx) const override {
        Json args = request.value("args", Json::object());
        std::string file = args.value("file", "");
        int line = args.value("line", 0);
        if (file.empty() || line <= 0)
            return Json({{"error","MISSING_FIELD"},{"message","args.file and args.line"}});

        bool compact = request.value("output", Json::object()).value("verbosity", "") == "compact";
        bool include_src = args.value("include_source", false);
        int ctx_lines = args.value("context_lines", compact && !include_src ? 3 : 8);

        std::ifstream in(file);
        if (!in) return Json({{"error","SOURCE_NOT_FOUND"},{"message",file}});
        std::vector<std::string> lines;
        std::string s;
        while (std::getline(in, s)) lines.push_back(s);
        if (line > (int)lines.size())
            return Json({{"error","INVALID_REQUEST"},{"message","line out of range"}});

        int begin = std::max(1, line - ctx_lines);
        int end = std::min((int)lines.size(), line + ctx_lines);
        nlohmann::json enclosing = detail::infer_enclosing_block(lines, line);

        Json out;
        out["summary"] = {{"file",file},{"line",line}};
        out["file"] = file;
        out["line"] = line;
        out["symbol"] = args.value("symbol", "");
        out["context_kind"] = enclosing.value("type", "unknown");
        out["enclosing"] = Json::parse(enclosing.dump());
        if (!compact || include_src) {
            nlohmann::json ctx = nlohmann::json::array();
            for (int i = begin; i <= end; ++i)
                ctx.push_back({{"line",i},{"text",lines[i-1]},{"hit",i == line}});
            out["context"] = Json::parse(ctx.dump());
        }
        return out;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_source_context_handler() {
    return std::unique_ptr<EngineActionHandler>(new SourceContextHandler);
}

}  // namespace xdebug_design
