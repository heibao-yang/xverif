#include "action_support.h"
#include "../protocol/protocol.h"
#include <cctype>
#include <climits>
#include <cstdlib>

namespace xdebug_waveform {

bool parse_apb_config(const Json& j, ApbConfig& c, std::string& err) {
    auto get = [&j](const char* key) -> std::string {
        auto it = j.find(key);
        return it != j.end() && it->is_string() ? it->get<std::string>() : "";
    };
    c.paddr = get("paddr");
    c.pwdata = get("pwdata");
    c.prdata = get("prdata");
    c.pwrite = get("pwrite");
    c.penable = get("penable");
    c.psel = get("psel");
    c.clk = get("clk");
    c.rst_n = get("rst_n");
    std::string edge = get("edge");
    if (edge.empty() || edge == "posedge") c.posedge = true;
    else if (edge == "negedge") c.posedge = false;
    else { err = "invalid APB edge: " + edge; return false; }
    if (c.paddr.empty() || c.pwdata.empty() || c.prdata.empty() || c.pwrite.empty() ||
        c.penable.empty() || c.psel.empty() || c.clk.empty() || c.rst_n.empty()) {
        err = "missing required APB config field";
        return false;
    }
    return true;
}

Json apb_config_json(const ApbConfig& c) {
    return {
        {"name", c.name}, {"paddr", c.paddr}, {"pwdata", c.pwdata}, {"prdata", c.prdata},
        {"pwrite", c.pwrite}, {"penable", c.penable}, {"psel", c.psel},
        {"clk", c.clk}, {"rst_n", c.rst_n}, {"edge", c.posedge ? "posedge" : "negedge"}
    };
}

bool parse_axi_config(const Json& j, AxiConfig& c, std::string& err) {
    auto get = [&j](const char* key) -> std::string {
        auto it = j.find(key);
        return it != j.end() && it->is_string() ? it->get<std::string>() : "";
    };
    c.awaddr = get("awaddr"); c.awid = get("awid"); c.awlen = get("awlen");
    c.awsize = get("awsize"); c.awburst = get("awburst"); c.awvalid = get("awvalid");
    c.awready = get("awready"); c.wdata = get("wdata"); c.wstrb = get("wstrb");
    c.wlast = get("wlast"); c.wvalid = get("wvalid"); c.wready = get("wready");
    c.bid = get("bid"); c.bresp = get("bresp"); c.bvalid = get("bvalid"); c.bready = get("bready");
    c.araddr = get("araddr"); c.arid = get("arid"); c.arlen = get("arlen");
    c.arsize = get("arsize"); c.arburst = get("arburst"); c.arvalid = get("arvalid");
    c.arready = get("arready"); c.rid = get("rid"); c.rdata = get("rdata");
    c.rresp = get("rresp"); c.rlast = get("rlast"); c.rvalid = get("rvalid");
    c.rready = get("rready"); c.clk = get("clk"); c.rst_n = get("rst_n");
    std::string edge = get("edge");
    if (edge.empty() || edge == "posedge") c.posedge = true;
    else if (edge == "negedge") c.posedge = false;
    else { err = "invalid AXI edge: " + edge; return false; }
    if (c.awaddr.empty() || c.awid.empty() || c.awlen.empty() || c.awsize.empty() ||
        c.awburst.empty() || c.awvalid.empty() || c.awready.empty() || c.wdata.empty() ||
        c.wstrb.empty() || c.wlast.empty() || c.wvalid.empty() || c.wready.empty() ||
        c.bid.empty() || c.bresp.empty() || c.bvalid.empty() || c.bready.empty() ||
        c.araddr.empty() || c.arid.empty() || c.arlen.empty() || c.arsize.empty() ||
        c.arburst.empty() || c.arvalid.empty() || c.arready.empty() || c.rid.empty() ||
        c.rdata.empty() || c.rresp.empty() || c.rlast.empty() || c.rvalid.empty() ||
        c.rready.empty() || c.clk.empty() || c.rst_n.empty()) {
        err = "missing required AXI config field";
        return false;
    }
    return true;
}

Json axi_config_json(const AxiConfig& c) {
    Json j;
    j["name"] = c.name;
    j["awaddr"] = c.awaddr; j["awid"] = c.awid; j["awlen"] = c.awlen;
    j["awsize"] = c.awsize; j["awburst"] = c.awburst; j["awvalid"] = c.awvalid;
    j["awready"] = c.awready; j["wdata"] = c.wdata; j["wstrb"] = c.wstrb;
    j["wlast"] = c.wlast; j["wvalid"] = c.wvalid; j["wready"] = c.wready;
    j["bid"] = c.bid; j["bresp"] = c.bresp; j["bvalid"] = c.bvalid; j["bready"] = c.bready;
    j["araddr"] = c.araddr; j["arid"] = c.arid; j["arlen"] = c.arlen;
    j["arsize"] = c.arsize; j["arburst"] = c.arburst; j["arvalid"] = c.arvalid;
    j["arready"] = c.arready; j["rid"] = c.rid; j["rdata"] = c.rdata;
    j["rresp"] = c.rresp; j["rlast"] = c.rlast; j["rvalid"] = c.rvalid;
    j["rready"] = c.rready; j["clk"] = c.clk; j["rst_n"] = c.rst_n;
    j["edge"] = c.posedge ? "posedge" : "negedge";
    return j;
}

bool parse_nonnegative_int(const Json& v, int& out) {
    if (!v.is_number_integer()) return false;
    long long n = v.get<long long>();
    if (n < 0 || n > INT_MAX) return false;
    out = static_cast<int>(n);
    return true;
}

bool parse_field_ref(const std::string& text, EventField& field) {
    size_t lb = text.find('[');
    size_t colon = text.find(':', lb == std::string::npos ? 0 : lb);
    size_t rb = text.find(']', colon == std::string::npos ? 0 : colon);
    if (lb == std::string::npos || colon == std::string::npos ||
        rb == std::string::npos || rb != text.size() - 1) return false;
    field.signal_alias = text.substr(0, lb);
    char* end = nullptr;
    long left = strtol(text.substr(lb + 1, colon - lb - 1).c_str(), &end, 10);
    if (!end || *end != '\0' || left < 0 || left > INT_MAX) return false;
    long right = strtol(text.substr(colon + 1, rb - colon - 1).c_str(), &end, 10);
    if (!end || *end != '\0' || right < 0 || right > INT_MAX) return false;
    field.left = static_cast<int>(left);
    field.right = static_cast<int>(right);
    return !field.signal_alias.empty();
}

bool parse_event_config(const Json& j, EventConfig& c, std::string& err) {
    if (!get_string(j, "clk", c.clk)) {
        err = "event config requires clk";
        return false;
    }
    get_string(j, "rst_n", c.rst_n);
    std::string edge = string_or(j, "edge", "posedge");
    if (edge == "posedge") c.posedge = true;
    else if (edge == "negedge") c.posedge = false;
    else { err = "invalid event edge: " + edge; return false; }
    auto sig_it = j.find("signals");
    if (sig_it == j.end() || !sig_it->is_object() || sig_it->empty()) {
        err = "event config requires non-empty signals object";
        return false;
    }
    for (auto it = sig_it->begin(); it != sig_it->end(); ++it) {
        if (!it.value().is_string()) {
            err = "event signal alias must map to string path: " + it.key();
            return false;
        }
        c.signals[it.key()] = it.value().get<std::string>();
    }
    auto fields_it = j.find("fields");
    if (fields_it != j.end()) {
        if (!fields_it->is_object()) {
            err = "event fields must be object";
            return false;
        }
        for (auto it = fields_it->begin(); it != fields_it->end(); ++it) {
            EventField f;
            if (it.value().is_string()) {
                if (!parse_field_ref(it.value().get<std::string>(), f)) {
                    err = "invalid field slice: " + it.key();
                    return false;
                }
            } else if (it.value().is_object()) {
                auto left_it = it.value().find("left");
                auto right_it = it.value().find("right");
                if (!get_string(it.value(), "signal", f.signal_alias) ||
                    left_it == it.value().end() || right_it == it.value().end() ||
                    !parse_nonnegative_int(*left_it, f.left) ||
                    !parse_nonnegative_int(*right_it, f.right)) {
                    err = "invalid field object: " + it.key();
                    return false;
                }
            } else {
                err = "invalid field definition: " + it.key();
                return false;
            }
            if (c.signals.find(f.signal_alias) == c.signals.end()) {
                err = "field references unknown signal alias: " + f.signal_alias;
                return false;
            }
            c.fields[it.key()] = f;
        }
    }
    return true;
}

Json event_config_json(const EventConfig& c) {
    Json j;
    j["name"] = c.name;
    j["clk"] = c.clk;
    if (!c.rst_n.empty()) j["rst_n"] = c.rst_n;
    j["edge"] = c.posedge ? "posedge" : "negedge";
    j["signals"] = c.signals;
    Json fields = Json::object();
    for (const auto& kv : c.fields) {
        fields[kv.first] = {
            {"signal", kv.second.signal_alias},
            {"left", kv.second.left},
            {"right", kv.second.right}
        };
    }
    j["fields"] = fields;
    return j;
}

bool load_config_json_arg(const Json& args, Json& config, std::string& err) {
    auto cfg_it = args.find("config");
    if (cfg_it != args.end()) {
        if (!cfg_it->is_object()) {
            err = "args.config must be an object";
            return false;
        }
        config = *cfg_it;
        return true;
    }
    std::string path;
    if (!get_string(args, "config_path", path)) {
        err = "missing args.config or args.config_path";
        return false;
    }
    std::string text;
    if (!read_file(path, text)) {
        err = "cannot read config_path: " + path;
        return false;
    }
    try {
        config = Json::parse(text);
    } catch (const std::exception& e) {
        err = std::string("failed to parse config_path: ") + e.what();
        return false;
    }
    return true;
}

char fmt_char(const Json& args) {
    std::string fmt = string_or(args, "format", "hex");
    if (fmt == "binary" || fmt == "bin") return 'B';
    if (fmt == "decimal" || fmt == "dec") return 'D';
    return 'H';
}

std::string arg_text(const Json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
    return v.dump();
}

bool query_value(const std::string& session_id,
                        const std::string& signal,
                        const std::string& time,
                        char fmt,
                        std::string& raw,
                        std::string& err) {
    std::string cmd = std::string(CMD_VALUE) + " " + signal + " " + time + " " + fmt;
    return capture_server_text(session_id, cmd, raw, err);
}

Json resolve_time_spec_json(const std::string& session_id,
                                   const std::string& spec,
                                   bool allow_max,
                                   std::string& err) {
    Json out;
    if (spec.empty()) return out;
    std::string cmd = std::string(CMD_TIME_RESOLVE) + " " + spec + (allow_max ? " allow_max" : "");
    if (!capture_server_json(session_id, cmd, out, err)) return Json();
    return out;
}

bool build_range_specs(const Json& args,
                              std::string& begin,
                              std::string& end,
                              bool& around_window,
                              std::string& err) {
    Json tr = args.value("time_range", Json::object());
    begin = string_or(tr, "begin", string_or(args, "begin", ""));
    end = string_or(tr, "end", string_or(args, "end", ""));
    around_window = false;
    if (!begin.empty() || !end.empty()) {
        if (begin.empty()) begin = "0ns";
        if (end.empty()) end = "max";
        return true;
    }
    std::string around = string_or(args, "around", "");
    if (around.empty()) {
        begin = "0ns";
        end = "max";
        return true;
    }
    std::string before = string_or(args, "before", "0ns");
    std::string after = string_or(args, "after", "0ns");
    if (before.empty()) before = "0ns";
    if (after.empty()) after = "0ns";
    if (!before.empty() && before[0] == '@') {
        err = "TIME_SPEC_INVALID: before must be a duration, not a TimeSpec";
        return false;
    }
    if (!after.empty() && after[0] == '@') {
        err = "TIME_SPEC_INVALID: after must be a duration, not a TimeSpec";
        return false;
    }
    begin = around + "-" + before;
    end = around + "+" + after;
    around_window = true;
    return true;
}

void fill_resolved_range(Json& out,
                                const std::string& sid,
                                const std::string& begin,
                                const std::string& end,
                                bool around_window,
                                std::string& err) {
    if (!out["data"].is_object()) out["data"] = Json::object();
    out["data"]["resolved_time_range"]["begin"] = resolve_time_spec_json(sid, begin, false, err);
    out["data"]["resolved_time_range"]["end"] = resolve_time_spec_json(sid, end, true, err);
    if (around_window) out["data"]["resolved_time_range"]["source"] = "around_window";
}

Tri tri_not(Tri v) {
    if (v == Tri::Unknown) return Tri::Unknown;
    return v == Tri::True ? Tri::False : Tri::True;
}

Tri tri_and(Tri a, Tri b) {
    if (a == Tri::False || b == Tri::False) return Tri::False;
    if (a == Tri::Unknown || b == Tri::Unknown) return Tri::Unknown;
    return Tri::True;
}

Tri tri_or(Tri a, Tri b) {
    if (a == Tri::True || b == Tri::True) return Tri::True;
    if (a == Tri::Unknown || b == Tri::Unknown) return Tri::Unknown;
    return Tri::False;
}

Tri value_to_bool(const std::string& raw) {
    if (contains_xz(raw)) return Tri::Unknown;
    return normalize_numeric(raw) == "0" ? Tri::False : Tri::True;
}

class ExprParser {
public:
    ExprParser(const std::string& text, const Json& values)
        : text_(text), values_(values), pos_(0), ok_(true) {}

    Tri parse() {
        Tri v = parse_or();
        skip_ws();
        if (pos_ != text_.size()) ok_ = false;
        return ok_ ? v : Tri::Unknown;
    }

    bool ok() const { return ok_; }

private:
    void skip_ws() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
    }

    bool eat(const std::string& token) {
        skip_ws();
        if (text_.compare(pos_, token.size(), token) == 0) {
            pos_ += token.size();
            return true;
        }
        return false;
    }

    std::string ident() {
        skip_ws();
        size_t start = pos_;
        if (pos_ < text_.size() && (std::isalpha(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '_')) {
            ++pos_;
            while (pos_ < text_.size() &&
                   (std::isalnum(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '_' || text_[pos_] == '.')) {
                ++pos_;
            }
        }
        return text_.substr(start, pos_ - start);
    }

    std::string literal() {
        skip_ws();
        size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '\'') {
            ++pos_;
            if (pos_ < text_.size()) ++pos_;
            while (pos_ < text_.size() && std::isxdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
            return text_.substr(start, pos_ - start);
        }
        while (pos_ < text_.size() &&
               (std::isalnum(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == 'x' ||
                text_[pos_] == 'X' || text_[pos_] == '_' || text_[pos_] == '\'')) {
            ++pos_;
        }
        return text_.substr(start, pos_ - start);
    }

    std::string value_for(const std::string& name) {
        auto it = values_.find(name);
        if (it == values_.end() || !it->is_object() || !it->contains("value")) {
            ok_ = false;
            return "";
        }
        return (*it)["value"]["value"].get<std::string>();
    }

    Tri parse_primary() {
        if (eat("(")) {
            Tri v = parse_or();
            if (!eat(")")) ok_ = false;
            return v;
        }
        if (eat("!")) return tri_not(parse_primary());

        std::string name = ident();
        if (name.empty()) {
            ok_ = false;
            return Tri::Unknown;
        }
        bool neq = false;
        if (eat("==") || (neq = eat("!="))) {
            std::string rhs = literal();
            if (rhs.empty()) {
                ok_ = false;
                return Tri::Unknown;
            }
            std::string lhs_val = value_for(name);
            if (contains_xz(lhs_val) || contains_xz(rhs)) return Tri::Unknown;
            bool eq = normalize_numeric(lhs_val) == normalize_numeric(rhs);
            return (neq ? !eq : eq) ? Tri::True : Tri::False;
        }
        return value_to_bool(value_for(name));
    }

    Tri parse_and() {
        Tri v = parse_primary();
        while (eat("&&")) v = tri_and(v, parse_primary());
        return v;
    }

    Tri parse_or() {
        Tri v = parse_and();
        while (eat("||")) v = tri_or(v, parse_and());
        return v;
    }

    std::string text_;
    Json values_;
    size_t pos_;
    bool ok_;
};

const char* tri_text(Tri v) {
    if (v == Tri::True) return "true";
    if (v == Tri::False) return "false";
    return "unknown";
}


Tri evaluate_expression(const std::string& expr, const Json& values, bool& ok) {
    ExprParser parser(expr, values);
    Tri value = parser.parse();
    ok = parser.ok();
    return value;
}

} // namespace xdebug_waveform
