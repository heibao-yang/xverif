#include "engine_action_registry.h"
#include "engine_globals.h"

#include "../../combined/active_trace_service.h"
#include "../../combined/active_trace_chain.h"
#include "../../api/text_response_builder.h"

#include <memory>
#include <sstream>

namespace xdebug_design {

// Registration functions from per-category handler files.
void register_waveform_handlers(EngineActionRegistry& r);
void register_protocol_handlers(EngineActionRegistry& r);
void register_design_handlers(EngineActionRegistry& r);
void register_stream_handlers(EngineActionRegistry& r);

// ═══════════════════════════════════════════════════════════════════════
// Combined action handlers: engine-owned action entries backed by helper payload builders.
// ═══════════════════════════════════════════════════════════════════════

namespace {

class ActiveDriverHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "trace.active_driver"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& request, EngineActionContext& ctx) const override {
        return xdebug::build_active_driver_payload(request, g_daidir_path, g_fsdb_path, g_fsdb_file);
    }
};

class ActiveDriverChainHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "trace.active_driver_chain"; }
    bool needs_design() const override { return true; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& request, EngineActionContext& ctx) const override {
        return xdebug::build_active_driver_chain_payload(request, g_daidir_path, g_fsdb_path, g_fsdb_file);
    }
};

} // namespace

// ═══════════════════════════════════════════════════════════════════════
// Registry implementation
// ═══════════════════════════════════════════════════════════════════════

void EngineActionRegistry::add(std::unique_ptr<EngineActionHandler> handler) {
    if (!handler) return;
    handlers_[handler->action_name()] = std::move(handler);
}

const EngineActionHandler* EngineActionRegistry::find(const std::string& action) const {
    auto it = handlers_.find(action);
    return it != handlers_.end() ? it->second.get() : nullptr;
}

const EngineActionRegistry& engine_action_registry() {
    static EngineActionRegistry* reg = []() {
        auto* r = new EngineActionRegistry();

        // ── Design handlers ──
        register_design_handlers(*r);

        // ── Waveform handlers ──
        register_waveform_handlers(*r);

        // ── Protocol handlers ──
        register_protocol_handlers(*r);

        // ── Stream handlers ──
        register_stream_handlers(*r);

        // ── Combined handlers ──
        r->add(std::unique_ptr<EngineActionHandler>(new ActiveDriverHandler));
        r->add(std::unique_ptr<EngineActionHandler>(new ActiveDriverChainHandler));

        return r;
    }();
    return *reg;
}

} // namespace xdebug_design
