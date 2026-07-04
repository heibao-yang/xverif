#pragma once

#include "json.hpp"

#include <string>

namespace xdebug_core {

using OrderedJson = nlohmann::ordered_json;

struct RuntimeSchemaValidationResult {
    bool ok = true;
    std::string code;
    std::string message;
    OrderedJson data = OrderedJson::object();
    OrderedJson summary = OrderedJson::object();
};

class RuntimeSchemaValidator {
public:
    RuntimeSchemaValidationResult validate_request(const std::string& action,
                                                   const OrderedJson& request,
                                                   const std::string& schema_ref = std::string()) const;
};

}  // namespace xdebug_core
