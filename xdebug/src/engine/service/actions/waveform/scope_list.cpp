#include "service/engine_action_handler.h"
#include "service/engine_action_registry.h"
#include "service/engine_globals.h"

#include "api/text_response_builder.h"
#include "design/protocol/protocol.h"
#include "waveform/server/fsdb_value_reader.h"
#include "waveform/event/event_manager.h"
#include "waveform/event/event_analyzer.h"
#include "waveform/list/list_manager.h"
#include "waveform/list/signal_list.h"
#include "waveform/export/waveform_exporter.h"
#include "waveform/common/xdebug_waveform_paths.h"
#include "waveform/service/action_support.h"
#include "waveform/service/rc_generator.h"
#include "waveform/value/logic_value.h"
#include "core/npi/time_contract.h"

#include "npi.h"
#include "npi_fsdb.h"
#include "npi_L1.h"
#include "npi_hdl.h"

#include <fstream>
#include <memory>
#include <algorithm>
#include <map>
#include <sstream>
#include <vector>

namespace xdebug_design {
namespace {
class ScopeListHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "scope.list"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }

    Json run(const Json& request, EngineActionContext& ctx) const override {
        Json args = request.value("args", Json::object());
        std::string path = args.value("path", std::string(""));
        bool recursive = args.value("recursive", true);
        int max_depth = args.value("max_depth", 3);
        Json limits = request.value("limits", Json::object());
        int max_rows = limits.value("max_rows", -1);

        FILE* fp = tmpfile();
        if (!fp) return err("INTERNAL_ERROR", "tmpfile failed");
        npi_fsdb_hier_tree_dump_sig(g_fsdb_file, fp, path.c_str(),
                                     recursive ? max_depth : 1);
        fflush(fp); rewind(fp);

        Json scopes = Json::array();
        Json signals = Json::array();
        char line[4096];
        while (fgets(line, sizeof(line), fp)) {
            size_t len = strlen(line);
            while (len > 0 && (line[len-1]=='\n' || line[len-1]=='\r')) line[--len]='\0';
            if (len == 0) continue;
            std::string s(line, len);
            bool is_scope = s.find("(scope)") != std::string::npos;
            size_t pos = s.find("  (");
            std::string name = (pos != std::string::npos) ? s.substr(0, pos) : s;
            if (is_scope) scopes.push_back(name);
            else signals.push_back(name);
        }
        fclose(fp);

        const size_t total_signals = signals.size();
        bool truncated = max_rows >= 0 && signals.size() > static_cast<size_t>(max_rows);
        if (truncated) {
            Json limited = Json::array();
            for (int i = 0; i < max_rows; ++i) limited.push_back(signals[i]);
            signals = limited;
        }
        Json out;
        out["path"] = path;
        out["recursive"] = recursive;
        out["summary"] = {
            {"path", path},
            {"recursive", recursive},
            {"returned_signal_count", static_cast<int>(signals.size())},
            {"total_signal_count", static_cast<int>(total_signals)},
            {"truncated", truncated}
        };
        out["scopes"] = scopes;
        out["signals"] = signals;
        return out;
    }

private:
    static Json err(const char* code, const std::string& msg) {
        Json e; e["error"] = code; e["message"] = msg; return e;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_scope_list_handler() {
    return std::unique_ptr<EngineActionHandler>(new ScopeListHandler);
}

}  // namespace xdebug_design
