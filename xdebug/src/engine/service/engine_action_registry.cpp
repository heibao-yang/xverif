#include "engine_action_registry.h"

#include <memory>

namespace xdebug_design {

// Registration functions from per-category handler files.
void register_waveform_handlers(EngineActionRegistry& r);
void register_protocol_handlers(EngineActionRegistry& r);
void register_design_handlers(EngineActionRegistry& r);
void register_stream_handlers(EngineActionRegistry& r);
void register_combined_handlers(EngineActionRegistry& r);

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
        register_combined_handlers(*r);

        return r;
    }();
    return *reg;
}

} // namespace xdebug_design
