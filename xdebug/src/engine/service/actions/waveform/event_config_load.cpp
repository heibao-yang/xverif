#include "service/engine_action_handler.h"
#include "service/engine_action_registry.h"
#include "service/engine_globals.h"
#include "event_action_helpers.h"

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
class EventConfigLoadHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "event.config.load"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& request, EngineActionContext& ctx) const override {
        using namespace xdebug_waveform;
        Json args = request.value("args", Json::object());
        std::string name = args.value("name", "");
        if (name.empty())
            return event_missing_field_error("event.config.load", "args.name", "event config name");

        nlohmann::json cfg_j; std::string err;
        if (!load_config_from_args(args, cfg_j, err))
            return make_handler_error(
                "INVALID_ARGUMENT",
                err,
                {{"invalid_arg", "args"},
                 {"expected", "config content through args.config or args.config_path"},
                 {"correct_example", event_config_example("event.config.load")},
                 {"example_note", "Example only; load config first, then use event.find/event.export with args.name."}});

        EventConfig cfg;
        if (!parse_event_config(cfg_j, cfg, err))
            return make_handler_error(
                "INVALID_ARGUMENT",
                err,
                {{"invalid_arg", "args.config"},
                 {"expected", "event config with clock/signals/expr compatible with the active waveform"},
                 {"correct_example", event_config_example("event.config.load")}});
        cfg.name = name;

        EventManager em;
        if (!em.create_event(g_session_id, g_fsdb_file_path, cfg))
            return make_handler_error("ACTION_FAILED", "failed to save event config",
                                      {{"cause_code", "CREATE_FAILED"}});

        Json out;
        out["summary"] = {{"name", name}, {"status", "loaded"}};
        out["name"] = name; out["status"] = "loaded";
        out["config"] = event_config_json(cfg);
        return out;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_event_config_load_handler() {
    return std::unique_ptr<EngineActionHandler>(new EventConfigLoadHandler);
}

}  // namespace xdebug_design
