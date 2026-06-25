#pragma once

#include "api/json_types.h"
#include "npi_fsdb.h"
#include "json.hpp"

namespace xdebug {

nlohmann::ordered_json build_active_driver_chain_payload(const Json& request,
                                                         const std::string& daidir,
                                                         const std::string& fsdb_path,
                                                         npiFsdbFileHandle fsdb);

} // namespace xdebug
