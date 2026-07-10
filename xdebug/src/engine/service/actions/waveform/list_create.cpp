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
class ListCreateHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "list.create"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& r, EngineActionContext& ctx) const override {
        Json a = r.value("args", Json::object());
        std::string n = a.value("name", "");
        if (n.empty())
            return list_missing_field_error("list.create", "args.name", "non-empty list name");
        xdebug_waveform::ListManager lm;
        if (!lm.create_list(xdebug_waveform::g_session_id, n))
            return make_handler_error(
                "PRECONDITION_FAILED",
                "failed to create list: " + n,
                {{"invalid_arg", "args.name"},
                 {"expected", "new list name not already used in this session"},
                 {"missing_name", n},
                 {"correct_example", list_action_example("list.create")},
                 {"cause_code", "CREATE_FAILED"}});
        // Optionally add initial signals
        Json sigs = a.value("signals", Json::array());
        Json added = Json::array();
        for (auto& s : sigs) if (s.is_string())
            if (lm.add_signal(xdebug_waveform::g_session_id, n, s.get<std::string>()))
                added.push_back(s);
        Json out;
        out["summary"] = {{"name", n}, {"status", "created"}, {"created", true},
                          {"signal_count", added.size()}};
        out["signals"] = added;
        return out;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_list_create_handler() {
    return std::unique_ptr<EngineActionHandler>(new ListCreateHandler);
}

}  // namespace xdebug_design
