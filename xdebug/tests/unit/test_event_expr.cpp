#include "waveform/event/event_expr.h"

#include <cassert>
#include <map>
#include <string>

using namespace xdebug_waveform;

static ExprTri eval_ok(const std::string& expr, const std::map<std::string, std::string>& values) {
    ExprTri result = ExprTri::Unknown;
    std::string error;
    bool ok = eval_event_expression(expr, values, result, error);
    assert(ok);
    assert(error.empty());
    return result;
}

static void eval_fails(const std::string& expr, const std::map<std::string, std::string>& values) {
    ExprTri result = ExprTri::Unknown;
    std::string error;
    bool ok = eval_event_expression(expr, values, result, error);
    assert(!ok);
    assert(!error.empty());
}

int main() {
    const std::map<std::string, std::string> values = {
        {"cnt", "10'h200"},
        {"small", "8'h0f"},
        {"xz", "8'hx0"},
        {"valid", "1"},
        {"ready", "0"}
    };

    assert(eval_ok("cnt >= 512", values) == ExprTri::True);
    assert(eval_ok("cnt > 511", values) == ExprTri::True);
    assert(eval_ok("cnt <= 512", values) == ExprTri::True);
    assert(eval_ok("cnt < 512", values) == ExprTri::False);
    assert(eval_ok("small < 16", values) == ExprTri::True);
    assert(eval_ok("small >= 4'hf", values) == ExprTri::True);
    assert(eval_ok("valid && !ready && cnt >= 10'h200", values) == ExprTri::True);
    assert(eval_ok("xz >= 1", values) == ExprTri::Unknown);

    eval_fails("cnt => 512", values);

    return 0;
}
