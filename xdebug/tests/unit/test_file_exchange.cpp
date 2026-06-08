#include "transport/file_exchange.h"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

using xdebug_core::Json;

static std::string join_path(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (a[a.size() - 1] == '/') return a + b;
    return a + "/" + b;
}

static Json read_json_file(const std::string& path) {
    std::ifstream in(path.c_str());
    std::ostringstream ss;
    ss << in.rdbuf();
    return Json::parse(ss.str());
}

int main() {
    std::string dir = "/tmp/xdebug_file_exchange_test_" + std::to_string(getpid());
    assert(xdebug_core::ensure_file_transport_layout(dir));

    std::string direct_req = join_path(join_path(dir, "requests"), "manual.json");
    assert(xdebug_core::atomic_write_json_file(direct_req, {{"id", "manual"}, {"request", {{"command", "PING"}}}}));

    std::string request_id;
    std::string claim_path;
    assert(xdebug_core::file_exchange_claim_one(dir, "agent_a", request_id, claim_path));
    assert(request_id == "manual");
    Json claimed = read_json_file(claim_path);
    assert(claimed["request"]["command"] == "PING");

    std::string second_id;
    std::string second_claim;
    assert(!xdebug_core::file_exchange_claim_one(dir, "agent_b", second_id, second_claim));

    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        std::string rid;
        std::string cpath;
        for (int i = 0; i < 100; ++i) {
            if (xdebug_core::file_exchange_claim_one(dir, "agent_child", rid, cpath)) {
                Json wrapper = read_json_file(cpath);
                Json response = {
                    {"id", rid},
                    {"ok", true},
                    {"status", "ok"},
                    {"response", {
                        {"payload", std::string("PONG ") + wrapper["request"].value("command", std::string())},
                        {"server_error", false}
                    }}
                };
                std::string rsp_path = join_path(join_path(dir, "responses"), rid + ".json");
                bool ok = xdebug_core::atomic_write_json_file(rsp_path, response);
                unlink(cpath.c_str());
                _exit(ok ? 0 : 2);
            }
            usleep(10000);
        }
        _exit(1);
    }

    xdebug_core::FileExchangeResult result =
        xdebug_core::file_exchange_send_request(dir, {{"command", "PING"}}, 1500);
    assert(result.ok);
    assert(result.status == "ok");
    assert(result.response["payload"] == "PONG PING");
    assert(result.response["server_error"] == false);

    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    std::string timeout_dir = dir + "_timeout";
    xdebug_core::FileExchangeResult timeout =
        xdebug_core::file_exchange_send_request(timeout_dir, {{"command", "PING"}}, 50);
    assert(!timeout.ok);
    assert(timeout.status == "timeout");

    return 0;
}
