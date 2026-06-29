#include "service/engine_action_handler.h"
#include "service/engine_action_registry.h"
#include "service/engine_globals.h"

#include "waveform/apb/apb_manager.h"
#include "waveform/apb/apb_analyzer.h"
#include "waveform/axi/axi_manager.h"
#include "waveform/axi/axi_analyzer.h"
#include "waveform/axi/axi_exporter.h"
#include "waveform/common/xdebug_waveform_paths.h"
#include "waveform/value/logic_value.h"

#include <fstream>
#include <memory>
#include <ctime>
#include <sstream>

namespace xdebug_design {
namespace {
class ApbConfigListHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "apb.config.list"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& r, EngineActionContext& ctx) const override {
        Json a = r.value("args", Json::object());
        std::string name = a.value("name", "");
        if (name.empty()) return Json({{"error","MISSING_FIELD"},{"message","args.name"}});
        xdebug_waveform::ApbManager am;
        xdebug_waveform::ApbConfig cfg;
        if (!am.get_apb(xdebug_waveform::g_session_id, name, cfg))
            return Json({{"error","CONFIG_NOT_FOUND"}});
        Json out; out["name"] = name;
        out["clk"] = cfg.clk; out["rst_n"] = cfg.rst_n;
        if (!cfg.pready.empty()) out["pready"] = cfg.pready;
        if (!cfg.pslverr.empty()) out["pslverr"] = cfg.pslverr;
        return out;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_apb_config_list_handler() {
    return std::unique_ptr<EngineActionHandler>(new ApbConfigListHandler);
}

}  // namespace xdebug_design
