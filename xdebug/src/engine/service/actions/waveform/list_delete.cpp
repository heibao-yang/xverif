#include "service/engine_action_handler.h"
#include "service/engine_action_registry.h"
#include "service/engine_globals.h"
#include "list_action_helpers.h"

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
class ListDeleteHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "list.delete"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& r, EngineActionContext& ctx) const override {
        Json a = r.value("args", Json::object());
        std::string n = a.value("name", "");
        if (n.empty())
            return list_missing_field_error("list.delete", "args.name", "name of a list created in this session");
        xdebug_waveform::ListManager lm;
        std::string signal;
        if (a.contains("signal") && a["signal"].is_string()) {
            signal = a["signal"].get<std::string>();
        } else if (a.contains("index")) {
            if (a["index"].is_number_integer()) {
                signal = std::to_string(a["index"].get<int>());
            } else if (a["index"].is_string()) {
                signal = a["index"].get<std::string>();
            } else {
                return list_invalid_arg_error("list.delete", "args.index", "integer index or signal string", a["index"]);
            }
        }
        if (signal.empty())
            return make_handler_error(
                "MISSING_FIELD",
                "args.signal or args.index is required",
                {{"invalid_arg", "args.signal"},
                 {"expected", "either args.signal or args.index"},
                 {"required_any_of", Json::array({"args.signal", "args.index"})},
                 {"correct_example", list_action_example("list.delete")},
                 {"example_note", "Example only; delete by one-based args.index or by exact args.signal."}});
        if (!lm.del_signal(xdebug_waveform::g_session_id, n, signal))
            return make_handler_error(
                "PRECONDITION_FAILED",
                "failed to delete list entry: " + signal,
                {{"invalid_arg", a.contains("index") ? "args.index" : "args.signal"},
                 {"expected", "existing list entry index or signal path"},
                 {"correct_example", list_action_example("list.delete")},
                 {"example_note", "Example only; delete by one-based args.index or by exact args.signal from list.show."},
                 {"cause_code", "DEL_FAILED"},
                 {"next_actions", Json::array({"Call list.show to inspect current one-based indexes and signal paths."})}});
        Json out;
        out["summary"] = {{"name", n}, {"deleted", true}, {"removed", signal}};
        return out;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_list_delete_handler() {
    return std::unique_ptr<EngineActionHandler>(new ListDeleteHandler);
}

}  // namespace xdebug_design
