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
        xdebug_waveform::ApbManager am;
        if (name.empty()) {
            auto configs = am.list_all(xdebug_waveform::g_session_id);
            Json arr = Json::array();
            for (const auto& cfg : configs) {
                arr.push_back({{"name", cfg.name},
                               {"sampling_mode", "clock_edge"},
                               {"clock", cfg.clock_sample.clock},
                               {"edge", xdebug_waveform::clock_edge_kind_text(cfg.clock_sample.edge)}});
            }
            return Json{{"summary", {{"count", configs.size()}}}, {"configs", arr}};
        }
        xdebug_waveform::ApbConfig cfg;
        if (!am.get_apb(xdebug_waveform::g_session_id, name, cfg))
            return Json({{"error","CONFIG_NOT_FOUND"},{"message",name}});
        Json out; out["name"] = name;
        out["sampling_mode"] = "clock_edge";
        out["clock"] = cfg.clock_sample.clock;
        out["edge"] = xdebug_waveform::clock_edge_kind_text(cfg.clock_sample.edge);
        if (cfg.clock_sample.edge != xdebug_waveform::ClockEdgeKind::Negedge)
            out["sample_point"] = xdebug_waveform::clock_sample_point_text(cfg.clock_sample.sample_point);
        out["rst_n"] = cfg.rst_n;
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
