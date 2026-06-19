#include "logging/action_log.h"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

using xdebug_core::Json;

static std::string read_file(const std::string& path) {
    std::ifstream in(path.c_str());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static Json read_last_json_line(const std::string& path) {
    std::ifstream in(path.c_str());
    std::string line;
    std::string last;
    while (std::getline(in, line)) {
        if (!line.empty()) last = line;
    }
    assert(!last.empty());
    return Json::parse(last);
}

int main() {
    std::string home = "/tmp/xdebug_action_log_test_" + std::to_string(getpid());
    setenv("HOME", home.c_str(), 1);

    Json request = {
        {"api_version", "xdebug.v1"},
        {"trace_id", "trace-case-a"},
        {"request_id", "case-a-1"},
        {"span_id", "span-dispatch"},
        {"action", "value.at"},
        {"target", {{"session_id", "case_a"}, {"daidir", "/tmp/foo.daidir"}}},
        {"args", {{"signal", "top.u.ready"}, {"time", "75ns"}, {"radix", "hex"},
                  {"include_trace", true}, {"samples", Json::array({1, 2, 3})}}},
        {"output", {{"verbosity", "compact"}}}
    };
    Json response = {
        {"ok", false},
        {"request_id", "case-a-1"},
        {"action", "value.at"},
        {"summary", {{"signal", "top.u.ready"}}},
        {"data", {{"trace", Json::array({1, 2, 3})}, {"small", true}}},
        {"error", {{"code", "SIGNAL_NOT_FOUND"}, {"message", "missing"}}},
        {"meta", {{"truncated", false}}}
    };

    Json sanitized = xdebug_core::sanitize_for_log(response);
    assert(sanitized["data"]["trace"] == "<omitted:large-field>");
    assert(sanitized.value("log_truncated", false));

    xdebug_core::update_public_session_manifest("case_a", "design", "/tmp/foo.daidir", "");
    std::string manifest_path = xdebug_core::public_session_dir("case_a") + "/session.json";
    Json manifest = Json::parse(read_file(manifest_path));
    assert(manifest["session_id"] == "case_a");
    assert(manifest["mode"] == "design");
    assert(manifest["daidir"] == "/tmp/foo.daidir");
    assert(manifest["logs"]["public_actions"] == xdebug_core::public_action_log_path("case_a"));
    assert(manifest["logs"]["public_stdio"] == xdebug_core::public_stdio_log_path("case_a"));

    Json summary = xdebug_core::request_summary_for_log(request);
    assert(summary["request_id"] == "case-a-1");
    assert(summary["trace_id"] == "trace-case-a");
    assert(summary["args"]["signal"] == "top.u.ready");
    assert(summary["args"]["time"] == "75ns");
    assert(summary["args"]["radix"] == "hex");
    assert(!summary["args"].contains("include_trace"));
    assert(!summary["args"].contains("samples"));

    xdebug_core::log_action_event("public", "xdebug", "case_a", "value.at", "end", false, 12,
                                  {{"request", xdebug_core::request_summary_for_log(request)},
                                   {"response", xdebug_core::response_summary_for_log(response)},
                                   {"request_compact", xdebug_core::sanitize_for_log(request)},
                                   {"response_compact", xdebug_core::sanitize_for_log(response)}});
    Json event = read_last_json_line(xdebug_core::public_action_log_path("case_a"));
    assert(event["layer"] == "public");
    assert(event["component"] == "xdebug");
    assert(event["session_id"] == "case_a");
    assert(event["request_id"] == "case-a-1");
    assert(event["trace_id"] == "trace-case-a");
    assert(event["action"] == "value.at");
    assert(event["phase"] == "end");
    assert(event["ok"] == false);
    assert(event["elapsed_ms"] == 12);

    xdebug_core::log_lifecycle_event("waveform", "case_a", "npi_fsdb_open.failed", false,
                                     {{"fsdb", "/tmp/a.fsdb"}});
    Json lifecycle_event = read_last_json_line(xdebug_core::component_log_path("waveform", "case_a", "lifecycle"));
    assert(lifecycle_event["component"] == "waveform");
    assert(lifecycle_event["phase"] == "npi_fsdb_open.failed");

    xdebug_core::log_stdio_event("case_a", "loop.validate_failed", false,
                                 {{"request_id", "stdio-1"}, {"action", "actions"},
                                  {"error", {{"code", "UNSUPPORTED_API_VERSION"}}}});
    Json stdio_event = read_last_json_line(xdebug_core::public_stdio_log_path("case_a"));
    assert(stdio_event["component"] == "xdebug");
    assert(stdio_event["request_id"] == "stdio-1");
    assert(stdio_event["phase"] == "loop.validate_failed");

    return 0;
}
