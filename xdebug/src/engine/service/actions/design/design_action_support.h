#pragma once

#include "service/engine_action_handler.h"

namespace xdebug_design {

Json run_trace_expand_action(const Json& request, EngineActionContext& ctx);
Json run_sequential_update_action(const Json& request, EngineActionContext& ctx);

} // namespace xdebug_design
