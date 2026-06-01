#include "backend/engine_adapter.h"
#include "api/response.h"
#include "logging/action_log.h"
#include "runtime/work_dir.h"

#include <cerrno>
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace xdebug {

namespace {

std::string engine_component(EngineKind kind) {
    return kind == EngineKind::Design ? "design" : "waveform";
}

std::string request_session_id_for_log(const Json& request) {
    Json target = request.value("target", Json::object());
    Json args = request.value("args", Json::object());
    auto get_str = [](const Json& obj, const char* key) -> std::string {
        auto it = obj.find(key);
        return it != obj.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    std::string sid = get_str(target, "session_id");
    if (!sid.empty()) return sid;
    sid = get_str(args, "session_id");
    if (!sid.empty()) return sid;
    sid = get_str(args, "name");
    if (!sid.empty()) return sid;
    sid = get_str(target, "name");
    return sid.empty() ? "adhoc" : sid;
}

} // namespace

EngineAdapter::EngineAdapter(const std::string& executable_dir)
    : executable_dir_(executable_dir) {}

std::string EngineAdapter::engine_path(EngineKind kind) const {
    return executable_dir_ + "/libexec/" +
           (kind == EngineKind::Design ? "xdebug-design-engine" : "xdebug-waveform-engine");
}

std::string EngineAdapter::engine_workdir(EngineKind kind) const {
    return runtime_work_dir(kind == EngineKind::Design ? "design" : "waveform");
}

Json engine_request(EngineKind kind, const Json& request) {
    Json forwarded = request;
    forwarded["api_version"] = "xdebug.internal.v1";
    if (forwarded.contains("target") && forwarded["target"].is_object()) {
        if (kind == EngineKind::Design) {
            if (forwarded["target"].contains("daidir")) {
                forwarded["target"]["dbdir"] = forwarded["target"]["daidir"];
                forwarded["target"].erase("daidir");
            }
            forwarded["target"].erase("fsdb");
        } else {
            forwarded["target"].erase("daidir");
        }
    }
    return forwarded;
}

bool EngineAdapter::invoke(EngineKind kind,
                           const Json& xdebug_request,
                           Json& response,
                           std::string& error) const {
    const std::string component = engine_component(kind);
    const std::string log_sid = request_session_id_for_log(xdebug_request);
    int input_pipe[2];
    int output_pipe[2];
    if (pipe(input_pipe) != 0 || pipe(output_pipe) != 0) {
        error = std::string("failed to create engine pipe: ") + std::strerror(errno);
        xdebug_core::log_lifecycle_event(component, log_sid, "engine.pipe_failed", false,
                                         {{"errno", errno}, {"message", error}});
        return false;
    }

    const std::string path = engine_path(kind);
    const std::string workdir = engine_workdir(kind);
    if (!ensure_runtime_work_dir(workdir)) {
        error = std::string("failed to create engine working directory: ") + workdir;
        xdebug_core::log_lifecycle_event(component, log_sid, "engine.workdir_failed", false,
                                         {{"workdir", workdir}, {"engine_path", path}});
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        return false;
    }
    pid_t pid = fork();
    if (pid < 0) {
        error = std::string("failed to fork engine: ") + std::strerror(errno);
        xdebug_core::log_lifecycle_event(component, log_sid, "engine.fork_failed", false,
                                         {{"errno", errno}, {"message", error}, {"engine_path", path}});
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        return false;
    }

    if (pid == 0) {
        dup2(input_pipe[0], STDIN_FILENO);
        dup2(output_pipe[1], STDOUT_FILENO);
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        if (chdir(workdir.c_str()) != 0) _exit(126);
        execl(path.c_str(), path.c_str(), "ai", "query", "-", static_cast<char*>(nullptr));
        _exit(127);
    }

    xdebug_core::log_lifecycle_event(component, log_sid, "engine.spawned", true,
                                     {{"pid", static_cast<int>(pid)}, {"engine_path", path}, {"workdir", workdir},
                                      {"action", xdebug_request.value("action", std::string())}});

    close(input_pipe[0]);
    close(output_pipe[1]);
    std::string input = engine_request(kind, xdebug_request).dump();
    input.push_back('\n');
    size_t written = 0;
    while (written < input.size()) {
        ssize_t n = write(input_pipe[1], input.data() + written, input.size() - written);
        if (n <= 0) break;
        written += static_cast<size_t>(n);
    }
    close(input_pipe[1]);

    std::string output;
    char buffer[4096];
    for (;;) {
        ssize_t n = read(output_pipe[0], buffer, sizeof(buffer));
        if (n <= 0) break;
        output.append(buffer, static_cast<size_t>(n));
    }
    close(output_pipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    try {
        response = normalize_engine_response(Json::parse(output));
    } catch (const std::exception& e) {
        error = std::string("invalid internal engine response: ") + e.what();
        if (WIFEXITED(status)) {
            error += " (exit=" + std::to_string(WEXITSTATUS(status)) + ")";
        }
        Json ctx = {{"message", error}, {"output", output}, {"action", xdebug_request.value("action", std::string())}};
        if (WIFEXITED(status)) ctx["exit_status"] = WEXITSTATUS(status);
        if (WIFSIGNALED(status)) ctx["signal"] = WTERMSIG(status);
        xdebug_core::log_lifecycle_event(component, log_sid, "engine.response_parse_failed", false, ctx);
        return false;
    }
    Json ctx = {{"action", xdebug_request.value("action", std::string())},
                {"ok", response.value("ok", false)}};
    if (WIFEXITED(status)) ctx["exit_status"] = WEXITSTATUS(status);
    if (WIFSIGNALED(status)) ctx["signal"] = WTERMSIG(status);
    xdebug_core::log_lifecycle_event(component, log_sid, "engine.completed", response.value("ok", true), ctx);
    return true;
}

} // namespace xdebug
