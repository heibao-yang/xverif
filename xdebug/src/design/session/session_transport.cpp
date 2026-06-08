#include "session_transport.h"
#include "../common/xdebug_design_paths.h"
#include "json.hpp"
#include "../protocol/protocol.h"
#include "transport/file_exchange.h"

#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <fcntl.h>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

namespace xdebug_design {

using Json = nlohmann::json;

std::string current_host_name() {
    char buf[256] = {};
    if (gethostname(buf, sizeof(buf) - 1) == 0 && buf[0]) return std::string(buf);
    return "localhost";
}

std::string generate_auth_token() {
    unsigned char bytes[24] = {};
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, bytes, sizeof(bytes));
        close(fd);
        if (n != static_cast<ssize_t>(sizeof(bytes))) memset(bytes, 0, sizeof(bytes));
    }
    if (bytes[0] == 0 && bytes[1] == 0) {
        unsigned long long seed = static_cast<unsigned long long>(time(nullptr)) ^
                                  (static_cast<unsigned long long>(getpid()) << 32);
        for (size_t i = 0; i < sizeof(bytes); ++i) {
            seed = seed * 6364136223846793005ULL + 1;
            bytes[i] = static_cast<unsigned char>(seed >> 24);
        }
    }
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(sizeof(bytes) * 2);
    for (unsigned char b : bytes) {
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 0xf]);
    }
    return out;
}

bool is_tcp_transport(const SessionInfo& session) {
    return session.transport == "tcp";
}

bool is_file_transport(const SessionInfo& session) {
    return session.transport == "file";
}

bool is_local_session_host(const SessionInfo& session) {
    return session.server_host.empty() || session.server_host == current_host_name() ||
           session.server_host == "localhost" || session.server_host == "127.0.0.1";
}

static int file_transport_request_timeout_ms() {
    const char* env = getenv("XDEBUG_FILE_TRANSPORT_TIMEOUT_MS");
    if (!env || !*env) return 300000;
    int value = atoi(env);
    return value > 0 ? value : 300000;
}

static int file_transport_ping_timeout_ms() {
    const char* env = getenv("XDEBUG_FILE_TRANSPORT_PING_TIMEOUT_MS");
    if (!env || !*env) return 2000;
    int value = atoi(env);
    return value > 0 ? value : 2000;
}

bool write_endpoint_file(const SessionInfo& session) {
    if (!xdebug_design_ensure_session_dir(session.session_id)) return false;
    Json root = {
        {"version", 1},
        {"endpoint", {
            {"transport", session.transport.empty() ? "uds" : session.transport},
            {"socket_path", session.socket_path},
            {"file_dir", session.file_dir},
            {"host", session.host},
            {"bind_host", session.bind_host},
            {"port", session.port},
            {"server_host", session.server_host},
            {"auth_token", session.auth_token}
        }}
    };
    return xdebug_core::atomic_write_json_file(xdebug_design_endpoint_path(session.session_id), root);
}

bool read_endpoint_file(const std::string& session_id, SessionInfo& endpoint) {
    FILE* fp = fopen(xdebug_design_endpoint_path(session_id).c_str(), "r");
    if (!fp) return false;
    std::string text;
    char buf[1024];
    while (fgets(buf, sizeof(buf), fp)) text += buf;
    fclose(fp);
    try {
        Json root = Json::parse(text);
        Json e = root.value("endpoint", Json::object());
        endpoint.session_id = session_id;
        endpoint.transport = e.value("transport", std::string("uds"));
        endpoint.socket_path = e.value("socket_path", xdebug_design_socket_path(session_id));
        endpoint.file_dir = e.value("file_dir", std::string());
        endpoint.host = e.value("host", std::string());
        endpoint.bind_host = e.value("bind_host", std::string());
        endpoint.port = e.value("port", 0);
        endpoint.server_host = e.value("server_host", std::string());
        endpoint.auth_token = e.value("auth_token", std::string());
        return true;
    } catch (...) {
        return false;
    }
}

static int connect_uds(const std::string& path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int connect_tcp(const std::string& host, int port) {
    if (host.empty() || port <= 0) return -1;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    std::string port_s = std::to_string(port);
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res) != 0) return -1;
    int fd = -1;
    for (struct addrinfo* p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static bool read_line_timeout(int fd, std::string& line) {
    line.clear();
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char c = 0;
    while (true) {
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) return false;
        if (c == '\n') return true;
        line.push_back(c);
        if (line.size() > 4096) return false;
    }
}

int connect_session_endpoint(const SessionInfo& session) {
    if (is_file_transport(session)) return -1;
    if (is_tcp_transport(session)) {
        return connect_tcp(session.host, session.port);
    }
    return connect_uds(session.socket_path.empty() ? xdebug_design_socket_path(session.session_id) : session.socket_path);
}

bool send_file_request_to_endpoint(const SessionInfo& session, const Json& request, Json& response, int timeout_ms) {
    if (!is_file_transport(session)) return false;
    std::string dir = session.file_dir.empty()
        ? xdebug_core::file_transport_dir(xdebug_design_session_dir(session.session_id))
        : session.file_dir;
    int effective_timeout_ms = timeout_ms > 0 ? timeout_ms : file_transport_request_timeout_ms();
    xdebug_core::FileExchangeResult result = xdebug_core::file_exchange_send_request(dir, request, effective_timeout_ms);
    if (!(result.status == "ok" || result.status == "action_error" || result.status == "server_error")) return false;
    if (!result.response.is_object()) return false;
    response = result.response;
    return true;
}

static bool request_simple(const SessionInfo& session, const std::string& action, Json& data) {
    if (is_file_transport(session)) {
        Json request = {{"api_version", INTERNAL_API_VERSION}, {"action", action}, {"args", Json::object()}};
        Json response;
        if (!send_file_request_to_endpoint(session, request, response, file_transport_ping_timeout_ms())) return false;
        bool ok = response.value("ok", false);
        data = response.value("data", Json::object());
        return ok;
    }
    int fd = connect_session_endpoint(session);
    if (fd < 0) return false;
    Json request = {{"api_version", INTERNAL_API_VERSION}, {"action", action}, {"args", Json::object()}};
    if (is_tcp_transport(session)) request["auth_token"] = session.auth_token;
    std::string msg = request.dump() + "\n";
    bool ok = write(fd, msg.c_str(), msg.size()) == static_cast<ssize_t>(msg.size());
    std::string line;
    if (ok) {
        ok = read_line_timeout(fd, line);
        if (ok) {
            try {
                Json response = Json::parse(line);
                ok = response.value("ok", false);
                data = response.value("data", Json::object());
            } catch (...) {
                ok = false;
            }
        }
    }
    close(fd);
    return ok;
}

bool ping_session_endpoint(const SessionInfo& session) {
    Json data;
    return request_simple(session, "server.ping", data) && data.value("pong", false);
}

bool protocol_version_matches_endpoint(const SessionInfo& session) {
    Json data;
    return request_simple(session, "server.version", data) &&
           data.value("api_version", std::string()) == INTERNAL_API_VERSION;
}

bool send_quit_to_endpoint(const SessionInfo& session) {
    Json data;
    return request_simple(session, "server.quit", data);
}

} // namespace xdebug_design
