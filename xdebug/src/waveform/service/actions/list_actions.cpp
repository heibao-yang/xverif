#include "../action_handler.h"
#include "../action_support.h"
#include "../command_builder.h"
#include "../../protocol/protocol.h"
#include "../../list/list_manager.h"

namespace xdebug_waveform {

namespace {

Json ok_out(const WaveformActionContext& ctx) {
    Json out = base_response(ctx.req, ctx.action, true, ctx.elapsed_ms);
    fill_session(out, ctx.info);
    return out;
}

int emit(const WaveformActionContext& ctx, const Json& out, int rc = 0) {
    print_json(finalize_response(ctx.req, out));
    return rc;
}

class ListCreateAction : public WaveformActionHandler {
public:
    const char* action_name() const override { return "list.create"; }

    int run(const WaveformActionContext& ctx) const override {
        ListManager lm;
        std::string name = string_or(ctx.args, "name", string_or(ctx.args, "list", ""));
        if (name.empty())
            return print_error_and_return(ctx.req, ctx.action, "MISSING_FIELD", "list.create requires args.name", ctx.elapsed_ms);
        if (!lm.create_list(ctx.sid, name))
            return print_error_and_return(ctx.req, ctx.action, "INTERNAL_ERROR", "failed to create list", ctx.elapsed_ms);
        Json out = ok_out(ctx);
        out["summary"] = {{"name", name}, {"status", "created"}};
        return emit(ctx, out);
    }
};

class ListAddAction : public WaveformActionHandler {
public:
    const char* action_name() const override { return "list.add"; }

    int run(const WaveformActionContext& ctx) const override {
        ListManager lm;
        std::string name = string_or(ctx.args, "name", string_or(ctx.args, "list", ""));
        if (name.empty()) lm.get_latest_list(ctx.sid, name);
        if (name.empty())
            return print_error_and_return(ctx.req, ctx.action, "MISSING_FIELD", "list.add requires args.name or latest list", ctx.elapsed_ms);
        std::string signal;
        if (!get_string(ctx.args, "signal", signal))
            return print_error_and_return(ctx.req, ctx.action, "MISSING_FIELD", "list.add requires args.signal", ctx.elapsed_ms);
        std::string err, payload;
        if (!capture_server_text(ctx.sid, CommandBuilder(CMD_SIGNAL_CHECK).arg(signal).build(), payload, err))
            return print_error_and_return(ctx.req, ctx.action, "SIGNAL_NOT_FOUND", err, ctx.elapsed_ms);
        if (!lm.add_signal(ctx.sid, name, signal))
            return print_error_and_return(ctx.req, ctx.action, "INTERNAL_ERROR", "failed to add signal", ctx.elapsed_ms);
        Json out = ok_out(ctx);
        out["summary"] = {{"name", name}, {"signal", signal}, {"status", "added"}};
        return emit(ctx, out);
    }
};

class ListDeleteAction : public WaveformActionHandler {
public:
    const char* action_name() const override { return "list.delete"; }

    int run(const WaveformActionContext& ctx) const override {
        ListManager lm;
        std::string name = string_or(ctx.args, "name", string_or(ctx.args, "list", ""));
        if (name.empty()) lm.get_latest_list(ctx.sid, name);
        if (name.empty())
            return print_error_and_return(ctx.req, ctx.action, "MISSING_FIELD", "list.delete requires args.name or latest list", ctx.elapsed_ms);
        std::string signal = string_or(ctx.args, "signal", string_or(ctx.args, "index", ""));
        if (signal.empty())
            return print_error_and_return(ctx.req, ctx.action, "MISSING_FIELD", "list.delete requires args.signal or args.index", ctx.elapsed_ms);
        if (!lm.del_signal(ctx.sid, name, signal))
            return print_error_and_return(ctx.req, ctx.action, "INTERNAL_ERROR", "failed to delete signal", ctx.elapsed_ms);
        Json out = ok_out(ctx);
        out["summary"] = {{"name", name}, {"removed", signal}};
        return emit(ctx, out);
    }
};

class ListShowAction : public WaveformActionHandler {
public:
    const char* action_name() const override { return "list.show"; }

    int run(const WaveformActionContext& ctx) const override {
        ListManager lm;
        std::string name = string_or(ctx.args, "name", string_or(ctx.args, "list", ""));
        if (name.empty()) lm.get_latest_list(ctx.sid, name);
        if (name.empty())
            return print_error_and_return(ctx.req, ctx.action, "MISSING_FIELD", "list.show requires args.name or latest list", ctx.elapsed_ms);
        SignalList list;
        if (!lm.get_list(ctx.sid, name, list))
            return print_error_and_return(ctx.req, ctx.action, "INVALID_REQUEST", "list not found", ctx.elapsed_ms);
        Json arr = Json::array();
        for (size_t i = 0; i < list.signals.size(); ++i)
            arr.push_back({{"index", static_cast<int>(i) + 1}, {"signal", list.signals[i]}});
        Json out = ok_out(ctx);
        out["summary"] = {{"name", name}, {"signal_count", arr.size()}};
        out["data"]["signals"] = arr;
        return emit(ctx, out);
    }
};

class ListValueAtAction : public WaveformActionHandler {
public:
    const char* action_name() const override { return "list.value_at"; }

    int run(const WaveformActionContext& ctx) const override {
        ListManager lm;
        std::string name = string_or(ctx.args, "name", string_or(ctx.args, "list", ""));
        if (name.empty()) lm.get_latest_list(ctx.sid, name);
        if (name.empty())
            return print_error_and_return(ctx.req, ctx.action, "MISSING_FIELD", "list.value_at requires args.name or latest list", ctx.elapsed_ms);
        std::string time;
        if (!get_string(ctx.args, "at", time) && !get_string(ctx.args, "time", time))
            return print_error_and_return(ctx.req, ctx.action, "MISSING_FIELD", "list.value_at requires args.time or args.at", ctx.elapsed_ms);
        Json data;
        std::string err;
        std::string cmd = CommandBuilder(CMD_LIST_VALUE).arg(name).arg(time).arg(std::string(1, fmt_char(ctx.args))).arg("json").build();
        bool ok = capture_server_json(ctx.sid, cmd, data, err);
        Json out = base_response(ctx.req, ctx.action, ok, ctx.elapsed_ms);
        fill_session(out, ctx.info);
        out["summary"] = {{"name", name}, {"time", time}};
        if (data.is_object() && data.contains("values") && data["values"].is_object()) {
            data["values"] = make_value_map(data["values"]);
        } else if (data.is_object()) {
            data = make_value_map(data);
        }
        if (compact_mode(ctx.req) && !bool_or(ctx.args, "include_raw", false)) {
            Json values = Json::object();
            Json raw_values = data.contains("values") ? data["values"] : data;
            if (raw_values.is_object()) {
                for (auto it = raw_values.begin(); it != raw_values.end(); ++it) {
                    if (it.value().is_object() && it.value().contains("value"))
                        values[it.key()] = it.value()["value"];
                    else
                        values[it.key()] = it.value();
                }
            }
            out["data"]["values"] = values;
        } else {
            out["data"] = data;
            Json resolved = resolve_time_spec_json(ctx.sid, time, false, err);
            if (!resolved.is_null()) out["data"]["resolved_time"] = resolved;
        }
        if (!ok) out["error"] = {{"code", "SIGNAL_NOT_FOUND"}, {"message", err}, {"recoverable", true}, {"candidates", Json::array()}, {"suggested_actions", Json::array()}};
        return emit(ctx, out, ok ? 0 : 1);
    }
};

class ListValidateAction : public WaveformActionHandler {
public:
    const char* action_name() const override { return "list.validate"; }

    int run(const WaveformActionContext& ctx) const override {
        ListManager lm;
        std::string name = string_or(ctx.args, "name", string_or(ctx.args, "list", ""));
        if (name.empty()) lm.get_latest_list(ctx.sid, name);
        if (name.empty())
            return print_error_and_return(ctx.req, ctx.action, "MISSING_FIELD", "list.validate requires args.name or latest list", ctx.elapsed_ms);
        Json data;
        std::string err;
        bool ok = capture_server_json(ctx.sid, CommandBuilder(CMD_LIST_VALIDATE).arg(name).arg("json").build(), data, err);
        Json out = base_response(ctx.req, ctx.action, ok, ctx.elapsed_ms);
        fill_session(out, ctx.info);
        out["summary"] = {{"name", name}, {"all_found", ok}};
        out["data"]["signals"] = data;
        if (!ok) out["error"] = {{"code", "SIGNAL_NOT_FOUND"}, {"message", err}, {"recoverable", true}, {"candidates", Json::array()}, {"suggested_actions", Json::array()}};
        return emit(ctx, out, ok ? 0 : 1);
    }
};

class ListDiffAction : public WaveformActionHandler {
public:
    const char* action_name() const override { return "list.diff"; }

    int run(const WaveformActionContext& ctx) const override {
        ListManager lm;
        std::string name = string_or(ctx.args, "name", string_or(ctx.args, "list", ""));
        if (name.empty()) lm.get_latest_list(ctx.sid, name);
        if (name.empty())
            return print_error_and_return(ctx.req, ctx.action, "MISSING_FIELD", "list.diff requires args.name or latest list", ctx.elapsed_ms);
        std::string begin, end, err;
        bool around_window = false;
        if (!build_range_specs(ctx.args, begin, end, around_window, err))
            return print_error_and_return(ctx.req, ctx.action, "TIME_SPEC_INVALID", err, ctx.elapsed_ms);
        std::string payload;
        if (!capture_server_text(ctx.sid, CommandBuilder(CMD_LIST_DIFF).arg(name).arg(begin).arg(end).build(), payload, err))
            return print_error_and_return(ctx.req, ctx.action, "WAVE_QUERY_FAILED", err, ctx.elapsed_ms);
        Json out = ok_out(ctx);
        out["summary"] = {{"name", name}, {"diff_time", payload}};
        out["data"]["time"] = payload;
        fill_resolved_range(out, ctx.sid, begin, end, around_window, err);
        return emit(ctx, out);
    }
};

class ListExportAction : public WaveformActionHandler {
public:
    const char* action_name() const override { return "list.export"; }

    int run(const WaveformActionContext& ctx) const override {
        ListManager lm;
        std::string name = string_or(ctx.args, "name", string_or(ctx.args, "list", ""));
        if (name.empty()) lm.get_latest_list(ctx.sid, name);
        if (name.empty())
            return print_error_and_return(ctx.req, ctx.action, "MISSING_FIELD", "list.export requires args.name or latest list", ctx.elapsed_ms);
        std::string begin, end, err;
        bool around_window = false;
        if (!build_range_specs(ctx.args, begin, end, around_window, err))
            return print_error_and_return(ctx.req, ctx.action, "TIME_SPEC_INVALID", err, ctx.elapsed_ms);
        Json begin_resolved = resolve_time_spec_json(ctx.sid, begin, false, err);
        if (begin_resolved.is_null())
            return print_error_and_return(ctx.req, ctx.action, "TIME_SPEC_INVALID", err, ctx.elapsed_ms);
        Json end_resolved = resolve_time_spec_json(ctx.sid, end, true, err);
        if (end_resolved.is_null())
            return print_error_and_return(ctx.req, ctx.action, "TIME_SPEC_INVALID", err, ctx.elapsed_ms);
        uint64_t begin_ps = begin_resolved.value("time_value", 0ULL);
        uint64_t end_ps = end_resolved.value("time_value", 0ULL);
        if (end_ps < begin_ps)
            return print_error_and_return(ctx.req, ctx.action, "TIME_SPEC_INVALID", "list.export end time is before begin time", ctx.elapsed_ms);
        const uint64_t kMinWindowPs = 256000ULL;
        if (end_ps - begin_ps < kMinWindowPs) {
            Json out = error_response(ctx.req, ctx.action, "TIME_RANGE_TOO_SMALL",
                "list.export requires a time range of at least 256ns; use list.value_at or value.batch_at for point reads",
                true, ctx.elapsed_ms);
            fill_session(out, ctx.info);
            out["data"]["min_window"] = "256ns";
            out["data"]["suggested_actions"] = Json::array({"list.value_at", "value.batch_at"});
            return emit(ctx, out, 1);
        }

        std::string format = string_or(ctx.args, "format", "u64bin");
        if (format != "u64bin" && format != "hex_tsv")
            return print_error_and_return(ctx.req, ctx.action, "INVALID_REQUEST", "format must be u64bin or hex_tsv", ctx.elapsed_ms);
        std::string output_dir = string_or(ctx.args, "output_dir", "-");
        if (output_dir.empty()) output_dir = "-";

        Json data;
        std::string cmd = CommandBuilder(CMD_LIST_EXPORT)
            .arg(name).arg(begin).arg(end).arg(output_dir).arg(format).build();
        bool ok = capture_server_json(ctx.sid, cmd, data, err);
        Json out = base_response(ctx.req, ctx.action, ok, ctx.elapsed_ms);
        fill_session(out, ctx.info);
        out["summary"] = Json::object();
        out["summary"]["name"] = name;
        out["summary"]["signal_count"] = ok && data.contains("signal_count") ? data["signal_count"] : Json(0);
        out["summary"]["row_count"] = ok && data.contains("row_count") ? data["row_count"] : Json(0);
        out["summary"]["format"] = ok && data.contains("format") ? data["format"] : Json(format);
        if (ok) {
            out["data"] = data;
        } else {
            out["error"] = {{"code", "EXPORT_FAILED"}, {"message", err}, {"recoverable", true}};
        }
        return emit(ctx, out, ok ? 0 : 1);
    }
};

} // namespace

std::unique_ptr<WaveformActionHandler> make_list_create_action() {
    return std::unique_ptr<WaveformActionHandler>(new ListCreateAction());
}
std::unique_ptr<WaveformActionHandler> make_list_add_action() {
    return std::unique_ptr<WaveformActionHandler>(new ListAddAction());
}
std::unique_ptr<WaveformActionHandler> make_list_delete_action() {
    return std::unique_ptr<WaveformActionHandler>(new ListDeleteAction());
}
std::unique_ptr<WaveformActionHandler> make_list_show_action() {
    return std::unique_ptr<WaveformActionHandler>(new ListShowAction());
}
std::unique_ptr<WaveformActionHandler> make_list_value_at_action() {
    return std::unique_ptr<WaveformActionHandler>(new ListValueAtAction());
}
std::unique_ptr<WaveformActionHandler> make_list_validate_action() {
    return std::unique_ptr<WaveformActionHandler>(new ListValidateAction());
}
std::unique_ptr<WaveformActionHandler> make_list_diff_action() {
    return std::unique_ptr<WaveformActionHandler>(new ListDiffAction());
}
std::unique_ptr<WaveformActionHandler> make_list_export_action() {
    return std::unique_ptr<WaveformActionHandler>(new ListExportAction());
}

} // namespace xdebug_waveform
