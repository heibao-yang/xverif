#pragma once

#include "json.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace xdebug_waveform {

using Json = nlohmann::ordered_json;

enum class ExprTri;

struct ExpressionValue {
    std::string bits;
    bool known = true;

    ExpressionValue() {}
    ExpressionValue(const std::string& bits_in, bool known_in)
        : bits(bits_in), known(known_in) {}

    int width() const { return static_cast<int>(bits.size()); }
};

struct ExpressionResult {
    bool ok = false;
    ExpressionValue value;
    std::string error_code;
    std::string message;
};

class Expression {
public:
    Expression();
    ~Expression();
    Expression(Expression&& other) noexcept;
    Expression& operator=(Expression&& other) noexcept;

    Expression(const Expression&) = delete;
    Expression& operator=(const Expression&) = delete;

    bool parse(const std::string& text, std::string& error);
    ExpressionResult evaluate(const std::map<std::string, ExpressionValue>& values) const;
    const std::string& text() const;
    const std::set<std::string>& aliases() const;

private:
    class Impl;
    Impl* impl_ = nullptr;
};

std::string expression_error_code_from_message(const std::string& message);
bool expression_alias_name_is_simple(const std::string& alias);
std::vector<std::string>
expression_aliases_that_look_like_paths(const std::set<std::string>& aliases);

std::map<std::string, ExpressionValue>
expression_stream_values_from_raw(const std::map<std::string, std::string>& values);

ExprTri expression_tri_from_value(const ExpressionValue& value);
bool expression_evaluate_tri(const std::string& expr,
                             const std::map<std::string, std::string>& values,
                             ExprTri& result,
                             std::string& error);

Json expression_value_json(const ExpressionValue& value);
bool expression_value_truthy(const ExpressionValue& value, bool unknown_default);
std::string expression_value_hex(const ExpressionValue& value);
bool expression_value_has_xz(const ExpressionValue& value);

} // namespace xdebug_waveform
