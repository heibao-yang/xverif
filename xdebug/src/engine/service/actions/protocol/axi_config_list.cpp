#include "service/engine_action_handler.h"
#include "service/engine_action_registry.h"
#include "service/engine_globals.h"
#include "protocol_action_helpers.h"

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
        xdebug_waveform::AxiManager am;
        if (name.empty()) {
            auto configs = am.list_all(xdebug_waveform::g_session_id);
            Json arr = Json::array();
            for (const auto& cfg : configs) {
                arr.push_back(axi_config_json(cfg));
            }
            return Json{{"summary", {{"count", configs.size()}}}, {"configs", arr}};
        }
        xdebug_waveform::AxiConfig cfg;
        if (!am.get_axi(xdebug_waveform::g_session_id, name, cfg))
            return protocol_config_not_found_error(action_name(), "axi", name);
        Json out;
        out["summary"] = {{"name", name}, {"status", "found"}};
        out["config"] = axi_config_json(cfg);
        return out;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_axi_config_list_handler() {
    return std::unique_ptr<EngineActionHandler>(new AxiConfigListHandler);
}

}  // namespace xdebug_design
