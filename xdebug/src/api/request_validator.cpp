#include "api/request_validator.h"
#include "api/response.h"

namespace xdebug {

ValidationResult RequestValidator::validate(const RequestEnvelope& request, const ActionSpec& spec) const {
    ValidationResult result;
    if (request.api_version != kApiVersion) {
        result.ok = false;
        result.code = "UNSUPPORTED_API_VERSION";
        result.message = "expected xdebug.v1";
        return result;
    }
    if (request.action != spec.name) {
        result.ok = false;
        result.code = "UNKNOWN_ACTION";
        result.message = "request action does not match ActionSpec";
        return result;
    }

    xdebug_core::RuntimeSchemaValidator validator;
    xdebug_core::RuntimeSchemaValidationResult schema_result =
        validator.validate_request(spec.name, request.raw, spec.request_schema);
    if (!schema_result.ok) {
        result.ok = false;
        result.code = schema_result.code.empty() ? "INVALID_REQUEST" : schema_result.code;
        result.message = schema_result.message;
        result.data = schema_result.data;
        result.summary = schema_result.summary;
        return result;
    }
    return result;
}

} // namespace xdebug
