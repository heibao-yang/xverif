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
class AxiConfigListHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "axi.config.list"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& r, EngineActionContext& ctx) const override {
        Json a = r.value("args", Json::object());
        std::string name = a.value("name", "");
        if (name.empty()) return Json({{"error","MISSING_FIELD"},{"message","args.name"}});
        xdebug_waveform::AxiManager am;
        xdebug_waveform::AxiConfig cfg;
        if (!am.get_axi(xdebug_waveform::g_session_id, name, cfg))
            return Json({{"error","CONFIG_NOT_FOUND"}});
        Json out; out["name"] = name;
        out["sampling_mode"] = "clock_edge";
        out["clock"] = cfg.clock_sample.clock;
        out["edge"] = xdebug_waveform::clock_edge_kind_text(cfg.clock_sample.edge);
        out["sample_offset"] = cfg.clock_sample.sample_offset_text.empty()
            ? "0ns" : cfg.clock_sample.sample_offset_text;
        out["rst_n"] = cfg.rst_n;
        return out;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_axi_config_list_handler() {
    return std::unique_ptr<EngineActionHandler>(new AxiConfigListHandler);
}

}  // namespace xdebug_design
