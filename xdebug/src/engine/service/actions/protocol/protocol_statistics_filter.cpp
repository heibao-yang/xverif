#include "protocol_statistics_filter.h"

#include "api/text_response_builder.h"
#include "waveform/value/logic_value.h"

#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>

namespace xdebug_design {
namespace {

bool parse_uint64_literal(const std::string& text, uint64_t& out,
                          std::string& message) {
    xdebug_waveform::LogicValue value;
    if (xdebug_waveform::is_legacy_0x_literal(text)) {
        value = xdebug_waveform::logic_value_from_fsdb_raw(text, 'h');
    } else {
        value = xdebug_waveform::parse_user_logic_literal(text);
    }
    if (!value.valid) {
        message = value.error;
        return false;
    }
    if (xdebug_waveform::logic_value_has_xz(value)) {
        message = "value literal must not contain X/Z: " + text;
        return false;
    }
    if (value.bits.size() > 64) {
        message = "value literal must be at most 64 bits: " + text;
        return false;
    }
    out = 0;
    for (char bit : value.bits) {
        out <<= 1U;
        if (bit == '1') out |= 1U;
    }
    return true;
}

bool parse_fsdb_uint64(const std::string& text, uint64_t& out) {
    xdebug_waveform::LogicValue value =
        xdebug_waveform::logic_value_from_fsdb_raw(text, 'h');
    if (!value.valid || xdebug_waveform::logic_value_has_xz(value) ||
        value.bits.size() > 64) {
        return false;
    }
    out = 0;
    for (char bit : value.bits) {
        out <<= 1U;
        if (bit == '1') out |= 1U;
    }
    return true;
}

std::string hex_value(uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::nouppercase << value;
    return out.str();
}

StatisticsMatch tri_and(StatisticsMatch lhs, StatisticsMatch rhs) {
    if (lhs == StatisticsMatch::No || rhs == StatisticsMatch::No)
        return StatisticsMatch::No;
    if (lhs == StatisticsMatch::Unresolved || rhs == StatisticsMatch::Unresolved)
        return StatisticsMatch::Unresolved;
    return StatisticsMatch::Yes;
}

bool parse_unique_values(const Json& values, const std::string& path,
                         std::vector<uint64_t>& out,
                         StatisticsFilterError& error) {
    std::set<uint64_t> seen;
    for (size_t index = 0; index < values.size(); ++index) {
        uint64_t value = 0;
        std::string message;
        if (!parse_uint64_literal(values[index].get<std::string>(), value, message)) {
            error = {path + "[" + std::to_string(index) + "]", message,
                     "known integer, hexadecimal, or SystemVerilog literal up to 64 bits"};
            return false;
        }
        if (!seen.insert(value).second) {
            error = {path, "values must remain unique after numeric normalization",
                     "non-empty queue of numerically distinct values"};
            return false;
        }
        out.push_back(value);
    }
    return true;
}

}  // namespace

bool parse_statistics_filter(const Json& args, bool allow_ids,
                             StatisticsFilter& out,
                             StatisticsFilterError& error) {
    out = StatisticsFilter();
    if (!args.contains("filter")) return true;

    const Json& filter = args["filter"];
    out.filter_applied = !filter.empty();
    const std::string direction = filter.value("direction", std::string("all"));
    if (direction == "read") out.direction = StatisticsDirection::Read;
    else if (direction == "write") out.direction = StatisticsDirection::Write;
    else out.direction = StatisticsDirection::All;

    if (filter.contains("ids")) {
        if (!allow_ids) {
            error = {"args.filter.ids", "APB statistics does not support transaction IDs",
                     "omit ids for APB statistics"};
            return false;
        }
        out.has_ids = true;
        if (!parse_unique_values(filter["ids"], "args.filter.ids", out.ids, error))
            return false;
    }

    if (!filter.contains("address")) return true;
    const Json& address = filter["address"];
    const std::string mode = address.value("mode", std::string());
    if (mode == "exact") {
        out.address_mode = StatisticsAddressMode::Exact;
        return parse_unique_values(address["values"], "args.filter.address.values",
                                   out.address_values, error);
    }

    std::string message;
    if (mode == "range") {
        out.address_mode = StatisticsAddressMode::Range;
        if (!parse_uint64_literal(address["begin"].get<std::string>(),
                                  out.address_begin, message)) {
            error = {"args.filter.address.begin", message,
                     "known integer, hexadecimal, or SystemVerilog literal up to 64 bits"};
            return false;
        }
        if (!parse_uint64_literal(address["end"].get<std::string>(),
                                  out.address_end, message)) {
            error = {"args.filter.address.end", message,
                     "known integer, hexadecimal, or SystemVerilog literal up to 64 bits"};
            return false;
        }
        if (out.address_begin > out.address_end) {
            error = {"args.filter.address.end", "address range begin must not exceed end",
                     "inclusive range with begin <= end"};
            return false;
        }
        return true;
    }

    out.address_mode = StatisticsAddressMode::Mask;
    if (!parse_uint64_literal(address["value"].get<std::string>(),
                              out.address_value, message)) {
        error = {"args.filter.address.value", message,
                 "known integer, hexadecimal, or SystemVerilog literal up to 64 bits"};
        return false;
    }
    if (!parse_uint64_literal(address["mask"].get<std::string>(),
                              out.address_mask, message)) {
        error = {"args.filter.address.mask", message,
                 "non-zero known integer, hexadecimal, or SystemVerilog literal up to 64 bits"};
        return false;
    }
    if (out.address_mask == 0) {
        error = {"args.filter.address.mask", "address mask must be non-zero",
                 "non-zero mask"};
        return false;
    }
    return true;
}

StatisticsMatch match_statistics_transaction(
    const StatisticsFilter& filter,
    const StatisticsTransactionView& transaction) {
    StatisticsMatch result = StatisticsMatch::Yes;
    if ((filter.direction == StatisticsDirection::Read && transaction.is_write) ||
        (filter.direction == StatisticsDirection::Write && !transaction.is_write)) {
        result = StatisticsMatch::No;
    }

    if (filter.address_mode != StatisticsAddressMode::None) {
        uint64_t address = 0;
        StatisticsMatch address_match = StatisticsMatch::Unresolved;
        if (parse_fsdb_uint64(transaction.address, address)) {
            bool matched = false;
            if (filter.address_mode == StatisticsAddressMode::Exact) {
                matched = std::find(filter.address_values.begin(),
                                    filter.address_values.end(), address) !=
                          filter.address_values.end();
            } else if (filter.address_mode == StatisticsAddressMode::Range) {
                matched = address >= filter.address_begin && address <= filter.address_end;
            } else {
                matched = (address & filter.address_mask) ==
                          (filter.address_value & filter.address_mask);
            }
            address_match = matched ? StatisticsMatch::Yes : StatisticsMatch::No;
        }
        result = tri_and(result, address_match);
    }

    if (filter.has_ids) {
        uint64_t id = 0;
        StatisticsMatch id_match = StatisticsMatch::Unresolved;
        if (parse_fsdb_uint64(transaction.id, id)) {
            id_match = std::find(filter.ids.begin(), filter.ids.end(), id) !=
                               filter.ids.end()
                           ? StatisticsMatch::Yes
                           : StatisticsMatch::No;
        }
        result = tri_and(result, id_match);
    }
    return result;
}

Json statistics_filter_json(const StatisticsFilter& filter, bool include_ids) {
    Json out;
    out["direction"] = filter.direction == StatisticsDirection::Read
                           ? "read"
                           : filter.direction == StatisticsDirection::Write ? "write" : "all";
    if (include_ids && filter.has_ids) {
        out["ids"] = Json::array();
        for (uint64_t id : filter.ids) out["ids"].push_back(std::to_string(id));
    }
    if (filter.address_mode == StatisticsAddressMode::Exact) {
        out["address"] = {{"mode", "exact"}, {"values", Json::array()}};
        for (uint64_t value : filter.address_values)
            out["address"]["values"].push_back(hex_value(value));
    } else if (filter.address_mode == StatisticsAddressMode::Range) {
        out["address"] = {{"mode", "range"},
                          {"begin", hex_value(filter.address_begin)},
                          {"end", hex_value(filter.address_end)}};
    } else if (filter.address_mode == StatisticsAddressMode::Mask) {
        out["address"] = {{"mode", "mask"},
                          {"value", hex_value(filter.address_value)},
                          {"mask", hex_value(filter.address_mask)}};
    }
    return out;
}

std::string statistics_ids_xout(const Json& ids) {
    std::string out = "[";
    for (size_t index = 0; index < ids.size(); ++index) {
        if (index) out += ", ";
        out += ids[index].get<std::string>();
    }
    out += "]";
    return out;
}

const char* statistics_unresolved_note() {
    return "因被引用的 address/ID 含 X/Z 或不可解析，导致无法判断是否匹配过滤条件的已完成事务数。";
}

std::string render_statistics_xout(const std::string& action,
                                   const Json& response) {
    xdebug::TextResponseBuilder out("xdebug");
    out.emit_header(action);
    const Json summary = response.value("summary", Json::object());
    out.emit_section("summary");
    for (Json::const_iterator it = summary.begin(); it != summary.end(); ++it)
        out.emit_kv(it.key(), it.value());

    const Json data = response.value("data", Json::object());
    const Json filter = data.value("filter", Json::object());
    out.emit_section("filter");
    out.emit_kv("direction", filter.value("direction", std::string("all")));
    if (filter.contains("ids"))
        out.emit_kv("ids", statistics_ids_xout(filter["ids"]));
    if (filter.contains("address")) {
        const Json& address = filter["address"];
        out.emit_kv("address_mode", address.value("mode", std::string()));
        const std::string mode = address.value("mode", std::string());
        if (mode == "exact") {
            out.emit_kv("address_values", statistics_ids_xout(address["values"]));
        } else if (mode == "range") {
            out.emit_kv("address_begin", address.value("begin", std::string()));
            out.emit_kv("address_end", address.value("end", std::string()));
        } else if (mode == "mask") {
            out.emit_kv("address_value", address.value("value", std::string()));
            out.emit_kv("address_mask", address.value("mask", std::string()));
        }
    }

    out.emit_section("notes");
    out.emit_kv("unresolved_transaction_count", statistics_unresolved_note());
    return out.str();
}

}  // namespace xdebug_design
