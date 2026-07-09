#include "service/engine_action_handler.h"
#include "service/engine_action_registry.h"
#include "service/engine_globals.h"
#include "waveform_action_support.h"
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
class ListShowHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "list.show"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& r, EngineActionContext& ctx) const override {
        Json a = r.value("args", Json::object());
        std::string n = a.value("name", "");
        if (n.empty())
            return list_missing_field_error("list.show", "args.name", "name of a list created in this session");
        xdebug_waveform::SignalList lst;
        if (!read_list_storage(n, lst))
            return list_not_found_error("list.show", n);
        Json out;
        Json arr = Json::array();
        for (size_t i = 0; i < lst.signals.size(); ++i)
            arr.push_back({{"index", static_cast<int>(i) + 1}, {"signal", lst.signals[i]}});
        out["summary"] = {{"name", n}, {"signal_count", static_cast<int>(lst.signals.size())}};
        out["signals"] = arr;
        return out;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_list_show_handler() {
    return std::unique_ptr<EngineActionHandler>(new ListShowHandler);
}

}  // namespace xdebug_design
