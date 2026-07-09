#include "waveform/common/expression.h"

#include <cassert>
#include <map>
#include <string>
#include <vector>

using namespace xdebug_waveform;

static Expression parse_ok(const std::string& text) {
    Expression expr;
    std::string error;
    bool ok = expr.parse(text, error);
    assert(ok);
    assert(error.empty());
    return expr;
}

static void parse_fails(const std::string& text) {
    Expression expr;
    std::string error;
    bool ok = expr.parse(text, error);
    assert(!ok);
    assert(!error.empty());
    assert(expression_error_code_from_message(error) == "EXPRESSION_PARSE_ERROR");
}

static ExpressionResult eval_ok(const std::string& text,
                                const std::map<std::string, ExpressionValue>& values) {
    Expression expr = parse_ok(text);
    ExpressionResult result = expr.evaluate(values);
    assert(result.ok);
    return result;
}

static ExpressionResult eval_fails(const std::string& text,
                                   const std::map<std::string, ExpressionValue>& values) {
    Expression expr = parse_ok(text);
    ExpressionResult result = expr.evaluate(values);
    assert(!result.ok);
    assert(!result.error_code.empty());
    assert(!result.message.empty());
    return result;
}

int main() {
    const std::map<std::string, ExpressionValue> values = {
        {"valid", {"1", true}},
        {"ready", {"0", true}},
        {"cnt", {"1000000000", true}},
        {"small", {"00001111", true}},
        {"payload", {"10100101", true}},
        {"xz", {"x0", false}}
    };

    ExpressionResult r = eval_ok("valid && !ready && cnt >= 10'h200", values);
    assert(r.value.bits == "1");
    assert(r.value.known);
    assert(expression_value_truthy(r.value, false));

    r = eval_ok("small < 8'd16", values);
    assert(r.value.bits == "1");

    r = eval_ok("payload[7:4] == 4'ha", values);
    assert(r.value.bits == "1");

    r = eval_ok("{payload[7:4], payload[3:0]}", values);
    assert(r.value.bits == "10100101");
    assert(expression_value_hex(r.value) == "8'ha5");

    r = eval_ok("~payload[3:0]", values);
    assert(r.value.bits == "1010");

    r = eval_ok("xz >= 1", values);
    assert(r.ok);
    assert(!r.value.known);
    assert(r.error_code == "UNKNOWN_VALUE");
    assert(expression_value_has_xz(r.value));

    r = eval_fails("missing == 1", values);
    assert(r.error_code == "MISSING_SIGNAL_VALUE");

    r = eval_fails("payload[9]", values);
    assert(r.error_code == "SELECT_OUT_OF_RANGE");

    parse_fails("cnt => 512");

    Expression expr = parse_ok("top.u.valid && ready");
    std::vector<std::string> bad_aliases = expression_aliases_that_look_like_paths(expr.aliases());
    assert(bad_aliases.size() == 1);
    assert(bad_aliases[0] == "top.u.valid");
    assert(expression_alias_name_is_simple("valid_0"));
    assert(!expression_alias_name_is_simple("top.u.valid"));

    std::map<std::string, ExpressionValue> raw_values = expression_stream_values_from_raw({
        {"cnt", "10'h200"},
        {"decimal", "16"},
        {"bits", "10xz"}
    });
    assert(raw_values["cnt"].bits == "1000000000");
    assert(raw_values["cnt"].known);
    assert(raw_values["decimal"].bits == "10000");
    assert(raw_values["bits"].bits == "10xz");
    assert(!raw_values["bits"].known);

    return 0;
}
