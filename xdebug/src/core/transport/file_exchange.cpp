#include "transport/file_exchange.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>
#include <algorithm>
#include <atomic>

namespace xdebug_core {

namespace {

long long now_us() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<long long>(tv.tv_sec) * 1000000LL + tv.tv_usec;
}

bool mkdir_p(const std::string& path) {
    if (path.empty()) return false;
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur.push_back(path[i]);
        if (path[i] != '/' && i + 1 != path.size()) continue;
        if (cur.empty() || cur == "/") continue;
        if (mkdir(cur.c_str(), 0700) != 0 && errno != EEXIST) return false;
    }
    return true;
}

std::string join_path(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (a[a.size() - 1] == '/') return a + b;
    return a + "/" + b;
}

bool read_json_file(const std::string& path, Json& out) {
    FILE* fp = fopen(path.c_str(), "r");
    if (!fp) return false;
    std::string text;
    char buf[4096];
    while (fgets(buf, sizeof(buf), fp)) text += buf;
    fclose(fp);
    try {
        out = Json::parse(text);
        return out.is_object();
    } catch (...) {
        return false;
    }
}

bool file_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

std::vector<std::string> list_json_files(const std::string& dir) {
    std::vector<std::string> out;
    DIR* dp = opendir(dir.c_str());
    if (!dp) return out;
    while (dirent* ent = readdir(dp)) {
        std::string name = ent->d_name;
        if (name.size() >= 5 && name.substr(name.size() - 5) == ".json") {
            out.push_back(name);
        }
    }
    closedir(dp);
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

std::string file_transport_dir(const std::string& session_dir) {
    return join_path(session_dir, "transport");
}

bool ensure_file_transport_layout(const std::string& dir) {
    if (!mkdir_p(dir)) return false;
    for (const char* sub : {"requests", "claims", "responses", "failed", "locks"}) {
        if (!mkdir_p(join_path(dir, sub))) return false;
    }
    return true;
}

bool atomic_write_json_file(const std::string& path, const Json& payload) {
    size_t slash = path.rfind('/');
    if (slash != std::string::npos && !mkdir_p(path.substr(0, slash))) return false;
    std::ostringstream tmp;
    tmp << path << ".tmp." << getpid() << "." << now_us();
    std::string tmp_path = tmp.str();
    std::string data = payload.dump(2) + "\n";
    int fd = open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    bool ok = write(fd, data.c_str(), data.size()) == static_cast<ssize_t>(data.size());
    if (ok) ok = fsync(fd) == 0;
    close(fd);
    if (!ok) {
        unlink(tmp_path.c_str());
        return false;
    }
    if (rename(tmp_path.c_str(), path.c_str()) != 0) {
        unlink(tmp_path.c_str());
        return false;
    }
    return true;
}

std::string make_file_request_id() {
    static std::atomic<unsigned long long> counter{0};
    std::ostringstream ss;
    ss << "req-" << now_us() << "-" << getpid() << "-" << counter.fetch_add(1);
    return ss.str();
}

FileExchangeResult file_exchange_send_request(const std::string& dir,
                                               const Json& request,
                                               int timeout_ms) {
    FileExchangeResult result;
    if (!ensure_file_transport_layout(dir)) {
        result.status = "layout_failed";
        result.message = "failed to create file transport directory";
        return result;
    }
    result.request_id = make_file_request_id();
    std::string req_path = join_path(join_path(dir, "requests"), result.request_id + ".json");
    std::string rsp_path = join_path(join_path(dir, "responses"), result.request_id + ".json");
    Json wrapper = {
        {"id", result.request_id},
        {"created_at_us", now_us()},
        {"deadline_us", now_us() + static_cast<long long>(timeout_ms) * 1000LL},
        {"request", request}
    };
    long long start = now_us();
    if (!atomic_write_json_file(req_path, wrapper)) {
        result.status = "write_failed";
        result.message = "failed to write file transport request";
        return result;
    }
    while (now_us() - start < static_cast<long long>(timeout_ms) * 1000LL) {
        if (file_exists(rsp_path)) {
            Json response_wrapper;
            if (!read_json_file(rsp_path, response_wrapper)) {
                result.status = "invalid_response";
                result.message = "file transport response is not valid JSON";
                return result;
            }
            unlink(rsp_path.c_str());
            result.elapsed_ms = static_cast<long>((now_us() - start) / 1000);
            result.ok = response_wrapper.value("ok", false);
            result.status = result.ok ? "ok" : response_wrapper.value("status", std::string("server_error"));
            result.message = response_wrapper.value("message", std::string());
            result.response = response_wrapper.value("response", Json::object());
            return result;
        }
        usleep(20000);
    }
    result.elapsed_ms = static_cast<long>((now_us() - start) / 1000);
    result.status = "timeout";
    result.message = "file transport request timed out";
    return result;
}

bool file_exchange_claim_one(const std::string& dir,
                             const std::string& agent_id,
                             std::string& request_id,
                             std::string& claim_path) {
    if (!ensure_file_transport_layout(dir)) return false;
    std::string req_dir = join_path(dir, "requests");
    std::string claim_dir = join_path(dir, "claims");
    for (const std::string& name : list_json_files(req_dir)) {
        request_id = name.substr(0, name.size() - 5);
        std::string src = join_path(req_dir, name);
        claim_path = join_path(claim_dir, request_id + "." + agent_id + ".json");
        if (rename(src.c_str(), claim_path.c_str()) == 0) return true;
    }
    request_id.clear();
    claim_path.clear();
    return false;
}

} // namespace xdebug_core
