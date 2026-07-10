#pragma once

#include "json.hpp"

#include <string>

namespace xdebug_design {

using json = nlohmann::json;

extern const char* const API_VERSION;
extern const char* const TOOL_VERSION;

std::string trim(const std::string& s);
bool starts_with(const std::string& s, const std::string& prefix);
std::string leaf_name(const std::string& signal);
std::string lower_copy(const std::string& s);
bool contains_word_like(const std::string& text, const std::string& token);
std::string next_token_after(const std::string& text, const std::string& key);

struct ExprSyntaxValidation {
    bool ok = true;
    std::string message;
    size_t offset = 0;
};

ExprSyntaxValidation validate_expr_syntax(const std::string& expr);
json parse_expr_ast(const std::string& expr);
std::string expr_op(const json& expr);
bool expr_mentions_signal(const json& expr, const std::string& signal);
json signal_array_from_ast(const json& expr);
json enrich_trace_payload(const json& request, const json& trace);
json make_trace_summary(const json& trace);

} // namespace xdebug_design
