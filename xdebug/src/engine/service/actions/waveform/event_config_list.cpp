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
class EventConfigListHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "event.config.list"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& request, EngineActionContext& ctx) const override {
        using namespace xdebug_waveform;
        Json args = request.value("args", Json::object());
        std::string name = args.value("name", "");
        EventManager em;
        if (name.empty()) {
            auto names = em.list_events(g_session_id, g_fsdb_file_path);
            Json arr = Json::array();
            for (size_t i = 0; i < names.size(); i++) arr.push_back(names[i]);
            return Json({{"count",static_cast<int>(arr.size())},{"events",arr}});
        }
        EventConfig cfg;
        if (!em.get_event(g_session_id, g_fsdb_file_path, name, cfg))
            return Json({{"error","CONFIG_NOT_FOUND"},{"message",name}});
        Json out; out["name"] = name;
        out["clk"] = cfg.clk; out["posedge"] = cfg.posedge;
        out["signals"] = cfg.signals;
        return out;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_event_config_list_handler() {
    return std::unique_ptr<EngineActionHandler>(new EventConfigListHandler);
}

}  // namespace xdebug_design
