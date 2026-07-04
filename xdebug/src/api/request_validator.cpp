#include "api/request_validator.h"
#include "api/response.h"

namespace xdebug {

namespace {

bool has_string_field(const Json& object, const std::string& field) {
    return object.is_object() && object.contains(field) && object[field].is_string() &&
           !object[field].get<std::string>().empty();
}

bool is_missing_session_selector(const RequestEnvelope& request) {
    if (request.action != "session.close" && request.action != "session.kill") return false;
    if (has_string_field(request.target, "session_id")) return false;
    if (has_string_field(request.args, "session_id")) return false;
    if (has_string_field(request.args, "id")) return false;
    return true;
}

void normalize_session_selector_error(const RequestEnvelope& request, ValidationResult& result) {
    if (!is_missing_session_selector(request)) return;
    const std::string expected = "target.session_id or args.session_id or args.id";
    result.message = expected + " is required";
    result.data = {
        {"invalid_arg", expected},
        {"expected", "non-empty string session id"}
    };
    result.summary = {
        {"invalid_arg", expected},
        {"message", result.message}
    };
}

} // namespace

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
        normalize_session_selector_error(request, result);
        return result;
    }
    return result;
}

} // namespace xdebug
