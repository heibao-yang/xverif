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
            return make_handler_error(
                "MISSING_FIELD",
                "args.file and args.line are required",
                {{"invalid_arg", file.empty() ? "args.file" : "args.line"},
                 {"expected", "readable source file and positive line number"},
                 {"correct_example", {{"api_version", "xdebug.v1"},
                                      {"action", "source.context"},
                                      {"args", {{"file", "rtl/top.sv"}, {"line", 42}, {"context_lines", 8}}}}},
                 {"example_note", "Example only; use the file and line returned by trace/source actions."}});

        int ctx_lines = args.value("context_lines", 3);

        std::ifstream in(file);
        if (!in) return make_handler_error(
            "SOURCE_NOT_FOUND",
            "source file not found: " + file,
            {{"invalid_arg", "args.file"},
             {"missing_name", file},
             {"missing_resource", "source file"},
             {"expected", "readable source file path"}});
        std::vector<std::string> lines;
        std::string s;
        while (std::getline(in, s)) lines.push_back(s);
        if (line > (int)lines.size())
            return make_handler_error(
                "INVALID_ARGUMENT",
                "line out of range",
                {{"invalid_arg", "args.line"},
                 {"expected", "line number within source file"},
                 {"received", line}});

        int begin = std::max(1, line - ctx_lines);
        int end = std::min((int)lines.size(), line + ctx_lines);
        nlohmann::json enclosing = detail::infer_enclosing_block(lines, line);

        Json out;
        out["summary"] = {{"file", file}, {"line", line},
                          {"context_begin", begin}, {"context_end", end},
                          {"context_kind", enclosing.value("type", "unknown")}};
        out["symbol"] = args.value("symbol", "");
        out["enclosing"] = Json::parse(enclosing.dump());
        nlohmann::json source_lines = nlohmann::json::array();
        for (int i = begin; i <= end; ++i)
            source_lines.push_back({{"line",i},{"text",lines[i-1]},{"hit",i == line}});
        out["context"] = Json::parse(source_lines.dump());
        return out;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_source_context_handler() {
    return std::unique_ptr<EngineActionHandler>(new SourceContextHandler);
}

}  // namespace xdebug_design
