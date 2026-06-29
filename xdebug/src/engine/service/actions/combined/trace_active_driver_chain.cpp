#include "service/engine_action_handler.h"
#include "service/engine_globals.h"

#include "combined/active_trace_service.h"
#include "combined/active_trace_chain.h"
#include "api/text_response_builder.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace xdebug_design {
namespace {

std::string scalar_text(const Json& object, const char* key) {
    if (!object.is_object() || !object.contains(key)) return std::string();
    const Json& value = object[key];
    if (!xdebug::is_xout_scalar_json(value)) return std::string();
    return xdebug::json_to_xout_value(value);
}

class ActiveDriverChainHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "trace.active_driver_chain"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& request, EngineActionContext& ctx) const override {
        return xdebug::build_active_driver_chain_payload(request, g_daidir_path, g_fsdb_path, g_fsdb_file);
    }

    std::string render_xout(const Json& response) const override {
        Json base_response = response;
        if (base_response.contains("data") && base_response["data"].is_object())
            base_response["data"].erase("common_blocks");
        std::string text = EngineActionHandler::render_xout(base_response);
        const Json data = response.value("data", Json::object());
        const Json chain_data = data.value("chain", Json::object());
        const Json chain = chain_data.value("chain", Json::array());
        if (!chain.is_array() || chain.empty()) return append_common_blocks_xout(text, response);

        std::vector<std::string> signals;
        for (const auto& node : chain) {
            std::string signal = scalar_text(node, "signal");
            if (!signal.empty()) signals.push_back(signal);
        }
        if (signals.empty()) return append_common_blocks_xout(text, response);

        std::ostringstream path;
        for (size_t i = 0; i < signals.size(); ++i) {
            if (i) path << " -> ";
            path << signals[i];
        }
        if (!text.empty() && text.back() != '\n') text.push_back('\n');
        text += "chain_path:\n  " + path.str() + "\n";
        return append_common_blocks_xout(text, response);
    }
};

} // namespace

std::unique_ptr<EngineActionHandler> make_trace_active_driver_chain_handler() {
    return std::unique_ptr<EngineActionHandler>(new ActiveDriverChainHandler);
}

} // namespace xdebug_design
