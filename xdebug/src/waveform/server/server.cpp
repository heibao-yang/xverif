#include "server_internal.h"
#include "logging/action_log.h"
#include "transport/file_exchange.h"

#include <thread>

namespace xdebug_waveform {

namespace {

bool run_file_command_through_handler(const std::string& command, std::string& payload, bool& server_error, bool& should_quit) {
    payload.clear();
    server_error = false;
    should_quit = false;
    int sv[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return false;
    bool handler_quit = false;
    std::thread worker([&]() {
        handle_client(sv[1], handler_quit);
        shutdown(sv[1], SHUT_WR);
        close(sv[1]);
    });
    std::string wire = command + "\n";
    send_all(sv[0], wire.c_str(), wire.size());
    shutdown(sv[0], SHUT_WR);
    std::string output;
    char buf[4096];
    while (true) {
        ssize_t n = read(sv[0], buf, sizeof(buf));
        if (n <= 0) break;
        output.append(buf, n);
    }
    close(sv[0]);
    worker.join();
    should_quit = handler_quit;
    const std::string end_marker(END_MARKER);
    size_t pos = output.find(end_marker);
    payload = pos == std::string::npos ? output : output.substr(0, pos);
    server_error = payload.compare(0, strlen(ERROR_PREFIX), ERROR_PREFIX) == 0;
    return pos != std::string::npos;
}

int file_transport_loop(const std::string& file_dir) {
    if (!xdebug_core::ensure_file_transport_layout(file_dir)) {
        xdebug_core::log_lifecycle_event("waveform", g_session_id, "transport.file_layout_failed", false,
                                         {{"file_dir", file_dir}});
        return 1;
    }
    SessionInfo endpoint;
    endpoint.session_id = g_session_id;
    endpoint.transport = "file";
    endpoint.file_dir = file_dir;
    endpoint.server_host = current_host_name();
    bool endpoint_ok = write_endpoint_file(endpoint);
    xdebug_core::log_lifecycle_event("waveform", g_session_id,
                                     endpoint_ok ? "endpoint.write_ok" : "endpoint.write_failed",
                                     endpoint_ok,
                                     {{"transport", endpoint.transport}, {"file_dir", endpoint.file_dir}});
    if (!endpoint_ok) return 1;

    const char* env_timeout = getenv("XDEBUG_WAVEFORM_IDLE_TIMEOUT_SEC");
    int idle_timeout = env_timeout ? atoi(env_timeout) : 1800;
    if (idle_timeout <= 0) idle_timeout = 1800;
    time_t last_active = time(nullptr);
    const std::string agent_id = current_host_name() + "-" + std::to_string(getpid());
    xdebug_core::log_lifecycle_event("waveform", g_session_id, "transport.file_loop_begin", true,
                                     {{"file_dir", file_dir}, {"idle_timeout_sec", idle_timeout}});
    while (true) {
        xdebug_core::Json worker = {{"agent_id", agent_id}, {"host", current_host_name()},
                                    {"pid", static_cast<int>(getpid())}};
        xdebug_core::atomic_write_json_file_ex(file_dir + "/heartbeat/" + agent_id + ".json",
                                               {{"version", xdebug_core::kFileRpcVersion},
                                                {"ok", true}, {"session_id", g_session_id},
                                                {"transport", "file"}, {"worker", worker},
                                                {"updated_at_us", xdebug_core::file_exchange_now_us()}},
                                               xdebug_core::AtomicWriteMode::Replace,
                                               file_dir + "/tmp");
        xdebug_core::file_exchange_scan_stale_claims(
            file_dir, agent_id, xdebug_core::file_exchange_claim_timeout_ms(0));
        xdebug_core::FileClaimResult claim = xdebug_core::file_exchange_claim_one(file_dir, agent_id);
        if (!claim.claimed) {
            if (time(nullptr) - last_active > idle_timeout) {
                xdebug_core::log_lifecycle_event("waveform", g_session_id, "server.idle_timeout_exit", true,
                                                 {{"idle_sec", static_cast<long>(time(nullptr) - last_active)},
                                                  {"timeout_sec", idle_timeout}});
                break;
            }
            usleep(static_cast<useconds_t>(xdebug_core::file_exchange_poll_interval_ms()) * 1000);
            continue;
        }
        std::string payload;
        bool server_error = true;
        bool quit = false;
        bool ok = claim.ready &&
                  claim.request.contains("command") && claim.request["command"].is_string() &&
                  run_file_command_through_handler(claim.request["command"].get<std::string>(),
                                                   payload, server_error, quit);
        xdebug_core::Json response = {
            {"payload", payload},
            {"server_error", server_error}
        };
        std::string status = ok && !server_error ? "ok" :
                             (ok ? "action_error" :
                              (claim.ready ? "server_error" : claim.status));
        std::string message = ok ? std::string() :
                              (claim.ready ? "invalid file transport request or response" : claim.message);
        xdebug_core::Json error = xdebug_core::Json::object();
        if (status != "ok") error = {{"code", status}, {"message", message}};
        if (claim.ready) {
            xdebug_core::file_exchange_complete_claim(file_dir, claim, response,
                                                      status == "ok", status, message, worker, error);
        }
        last_active = time(nullptr);
        if (quit) break;
    }
    xdebug_core::log_lifecycle_event("waveform", g_session_id, "transport.file_loop_end", true);
    return 0;
}

} // namespace

int server_main(int argc, char** argv) {
    // argv: [exe, session_id, fsdb_file]
    if (argc < 3) {
        fprintf(stderr, "Server mode requires session_id and fsdb_file arguments\n");
        return 1;
    }

    int arg_idx = 1;

    // Parse session ID
    g_session_id = argv[arg_idx];
    if (!SessionRegistry::is_valid_session_name(g_session_id)) {
        fprintf(stderr, "Invalid session ID: %s\n", argv[arg_idx]);
        return 1;
    }
    server_debug_open_log();
    server_debug_log("server_main: parsed_session_id=%s", g_session_id.c_str());
    xdebug_core::log_lifecycle_event("waveform", g_session_id, "server.start", true,
                                     {{"argc", argc}});
    arg_idx++;

    // Parse FSDB file
    const char* fsdb_file = argv[arg_idx];
    g_fsdb_file_path = fsdb_file;
    arg_idx++;
    while (arg_idx < argc) {
        std::string opt = argv[arg_idx++];
        if (opt == "--transport" && arg_idx < argc) g_transport = argv[arg_idx++];
        else if (opt == "--bind" && arg_idx < argc) g_bind_host = argv[arg_idx++];
        else if (opt == "--host" && arg_idx < argc) g_host = argv[arg_idx++];
        else if (opt == "--port" && arg_idx < argc) g_port = atoi(argv[arg_idx++]);
        else if (opt == "--auth" && arg_idx < argc) g_auth_token = argv[arg_idx++];
    }
    if (g_transport.empty()) g_transport = "uds";
    if (g_transport == "tcp") {
        if (g_bind_host.empty()) g_bind_host = "127.0.0.1";
        if (g_host.empty()) {
            g_host = (g_bind_host == "0.0.0.0" || g_bind_host == "::") ? current_host_name() : g_bind_host;
        }
    }
    server_debug_log("server_main: transport=%s bind=%s host=%s port=%d",
                     g_transport.c_str(), g_bind_host.c_str(), g_host.c_str(), g_port);
    xdebug_core::log_lifecycle_event("waveform", g_session_id, "server.transport_config", true,
                                     {{"transport", g_transport}, {"bind_host", g_bind_host},
                                      {"host", g_host}, {"port", g_port}});
    stat_fsdb(g_fsdb_mtime, g_fsdb_size, g_fsdb_dev, g_fsdb_inode);
    server_debug_log("server_main: fsdb=%s stat mtime=%ld size=%lld dev=%llu inode=%llu",
                     fsdb_file, g_fsdb_mtime, g_fsdb_size, g_fsdb_dev, g_fsdb_inode);
    xdebug_core::log_lifecycle_event("waveform", g_session_id, "server.fsdb_stat", true,
                                     {{"fsdb", fsdb_file}, {"mtime", g_fsdb_mtime}, {"size", g_fsdb_size},
                                      {"dev", g_fsdb_dev}, {"inode", g_fsdb_inode}});

    // Redirect stdout to capture NPI init messages, but keep a copy
    int stdout_copy = dup(STDOUT_FILENO);

    // Initialize NPI
    int npi_argc = 1;
    char** npi_argv = argv;
    server_debug_log("server_main: npi_init_begin");
    xdebug_core::log_lifecycle_event("waveform", g_session_id, "npi_init.begin", true);
    int result = npi_init(npi_argc, npi_argv);
    if (result == 0) {
        server_debug_log("server_main: npi_init_failed");
        xdebug_core::log_lifecycle_event("waveform", g_session_id, "npi_init.failed", false);
        dprintf(stdout_copy, "[Session %s] ERROR: npi_init failed\n", g_session_id.c_str());
        close(stdout_copy);
        if (g_debug_log) {
            fclose(g_debug_log);
            g_debug_log = nullptr;
        }
        return 1;
    }
    server_debug_log("server_main: npi_init_ok");
    xdebug_core::log_lifecycle_event("waveform", g_session_id, "npi_init.ok", true);

    server_debug_log("server_main: npi_fsdb_open_begin fsdb=%s", fsdb_file);
    xdebug_core::log_lifecycle_event("waveform", g_session_id, "npi_fsdb_open.begin", true,
                                     {{"fsdb", fsdb_file}});
    g_fsdb_file = npi_fsdb_open(fsdb_file);
    if (!g_fsdb_file) {
        server_debug_log("server_main: npi_fsdb_open_failed fsdb=%s", fsdb_file);
        xdebug_core::log_lifecycle_event("waveform", g_session_id, "npi_fsdb_open.failed", false,
                                         {{"fsdb", fsdb_file}});
        dprintf(stdout_copy, "[Session %s] ERROR: npi_fsdb_open failed: %s\n", g_session_id.c_str(), fsdb_file);
        npi_end();
        close(stdout_copy);
        if (g_debug_log) {
            fclose(g_debug_log);
            g_debug_log = nullptr;
        }
        return 1;
    }
    server_debug_log("server_main: npi_fsdb_open_ok");
    xdebug_core::log_lifecycle_event("waveform", g_session_id, "npi_fsdb_open.ok", true,
                                     {{"fsdb", fsdb_file}});

    npiFsdbTime minTime, maxTime;
    npi_fsdb_min_time(g_fsdb_file, &minTime);
    npi_fsdb_max_time(g_fsdb_file, &maxTime);
    server_debug_log("server_main: fsdb_time min=%llu max=%llu", minTime, maxTime);
    xdebug_core::log_lifecycle_event("waveform", g_session_id, "npi_fsdb_time", true,
                                     {{"min", static_cast<unsigned long long>(minTime)},
                                      {"max", static_cast<unsigned long long>(maxTime)}});

    dprintf(stdout_copy, "[Session %s] Ready (FSDB: %llu ~ %llu)\n", g_session_id.c_str(), minTime, maxTime);
    fflush(stdout);
    close(stdout_copy);

    // Now daemonize I/O
    server_debug_log("server_main: daemonize_io");
    daemonize_io();

    // Set up signal handlers
    signal(SIGTERM, cleanup_and_exit);
    signal(SIGINT, cleanup_and_exit);

    get_sock_path(g_sock_path, g_session_id);
    if (g_transport == "file") {
        std::string file_dir = xdebug_core::file_transport_dir(xdebug_waveform_session_dir(g_session_id));
        int rc = file_transport_loop(file_dir);
        if (g_fsdb_file) {
            npi_fsdb_close(g_fsdb_file);
            g_fsdb_file = nullptr;
        }
        {
            SessionRegistry registry;
            registry.remove(g_session_id);
        }
        npi_end();
        if (g_debug_log) {
            server_debug_log("server_main: normal_exit");
            xdebug_core::log_lifecycle_event("waveform", g_session_id, "server.normal_exit", rc == 0);
            fclose(g_debug_log);
            g_debug_log = nullptr;
        }
        return rc;
    }

    if (g_transport == "tcp") {
        server_debug_log("server_main: tcp_socket_create_begin");
        struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;
        std::string port_s = std::to_string(g_port);
        struct addrinfo* res = nullptr;
        int gai = getaddrinfo(g_bind_host.c_str(), port_s.c_str(), &hints, &res);
        if (gai != 0) {
            server_debug_log("server_main: tcp_getaddrinfo_failed %s", gai_strerror(gai));
            xdebug_core::log_lifecycle_event("waveform", g_session_id, "transport.tcp_getaddrinfo_failed", false,
                                             {{"message", gai_strerror(gai)}, {"bind_host", g_bind_host}, {"port", g_port}});
            npi_fsdb_close(g_fsdb_file);
            npi_end();
            return 1;
        }
        for (struct addrinfo* p = res; p; p = p->ai_next) {
            g_srv_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (g_srv_fd < 0) continue;
            int one = 1;
            setsockopt(g_srv_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
            if (bind(g_srv_fd, p->ai_addr, p->ai_addrlen) == 0) break;
            close(g_srv_fd);
            g_srv_fd = -1;
        }
        freeaddrinfo(res);
        if (g_srv_fd < 0) {
            server_debug_log("server_main: tcp_bind_failed errno=%d(%s)", errno, strerror(errno));
            xdebug_core::log_lifecycle_event("waveform", g_session_id, "transport.tcp_bind_failed", false,
                                             {{"errno", errno}, {"message", strerror(errno)}, {"bind_host", g_bind_host}, {"port", g_port}});
            npi_fsdb_close(g_fsdb_file);
            npi_end();
            return 1;
        }
        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);
        if (getsockname(g_srv_fd, reinterpret_cast<struct sockaddr*>(&ss), &slen) == 0) {
            if (ss.ss_family == AF_INET) {
                g_port = ntohs(reinterpret_cast<struct sockaddr_in*>(&ss)->sin_port);
            } else if (ss.ss_family == AF_INET6) {
                g_port = ntohs(reinterpret_cast<struct sockaddr_in6*>(&ss)->sin6_port);
            }
        }
        server_debug_log("server_main: tcp_bind_ok host=%s port=%d", g_host.c_str(), g_port);
        xdebug_core::log_lifecycle_event("waveform", g_session_id, "transport.tcp_bind_ok", true,
                                         {{"host", g_host}, {"port", g_port}});
    } else {
        server_debug_log("server_main: uds_socket_create_begin");
        g_srv_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (g_srv_fd < 0) {
            server_debug_log("server_main: socket_create_failed errno=%d(%s)", errno, strerror(errno));
            xdebug_core::log_lifecycle_event("waveform", g_session_id, "transport.uds_socket_failed", false,
                                             {{"errno", errno}, {"message", strerror(errno)}});
            npi_fsdb_close(g_fsdb_file);
            npi_end();
            if (g_debug_log) {
                fclose(g_debug_log);
                g_debug_log = nullptr;
            }
            return 1;
        }
        unlink(g_sock_path);
        server_debug_log("server_main: socket_path=%s", g_sock_path);
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, g_sock_path, sizeof(addr.sun_path) - 1);
        if (bind(g_srv_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            server_debug_log("server_main: socket_bind_failed errno=%d(%s)", errno, strerror(errno));
            xdebug_core::log_lifecycle_event("waveform", g_session_id, "transport.uds_bind_failed", false,
                                             {{"socket_path", g_sock_path}, {"errno", errno}, {"message", strerror(errno)}});
            close(g_srv_fd);
            npi_fsdb_close(g_fsdb_file);
            npi_end();
            if (g_debug_log) {
                fclose(g_debug_log);
                g_debug_log = nullptr;
            }
            return 1;
        }
        chmod(g_sock_path, 0600);
        server_debug_log("server_main: uds_bind_ok");
        xdebug_core::log_lifecycle_event("waveform", g_session_id, "transport.uds_bind_ok", true,
                                         {{"socket_path", g_sock_path}});
    }

    server_debug_log("server_main: socket_listen_begin");
    if (listen(g_srv_fd, 8) < 0) {
        server_debug_log("server_main: socket_listen_failed errno=%d(%s)", errno, strerror(errno));
        xdebug_core::log_lifecycle_event("waveform", g_session_id, "transport.listen_failed", false,
                                         {{"transport", g_transport}, {"socket_path", g_sock_path},
                                          {"host", g_host}, {"port", g_port}, {"errno", errno}, {"message", strerror(errno)}});
        close(g_srv_fd);
        unlink(g_sock_path);
        npi_fsdb_close(g_fsdb_file);
        npi_end();
        if (g_debug_log) {
            fclose(g_debug_log);
            g_debug_log = nullptr;
        }
        return 1;
    }
    server_debug_log("server_main: socket_listen_ok");
    xdebug_core::log_lifecycle_event("waveform", g_session_id, "transport.listen_ok", true,
                                     {{"transport", g_transport}, {"socket_path", g_sock_path}, {"host", g_host}, {"port", g_port}});

    {
        SessionInfo endpoint;
        endpoint.session_id = g_session_id;
        endpoint.transport = g_transport;
        endpoint.socket_path = g_sock_path;
        endpoint.host = g_transport == "tcp" ? g_host : "";
        endpoint.bind_host = g_transport == "tcp" ? g_bind_host : "";
        endpoint.port = g_transport == "tcp" ? g_port : 0;
        endpoint.server_host = current_host_name();
        endpoint.auth_token = g_transport == "tcp" ? g_auth_token : "";
        if (!write_endpoint_file(endpoint)) {
            server_debug_log("server_main: endpoint_write_failed");
            xdebug_core::log_lifecycle_event("waveform", g_session_id, "endpoint.write_failed", false);
        } else {
            server_debug_log("server_main: endpoint_write_ok transport=%s host=%s port=%d",
                             endpoint.transport.c_str(), endpoint.host.c_str(), endpoint.port);
            xdebug_core::log_lifecycle_event("waveform", g_session_id, "endpoint.write_ok", true,
                                             {{"transport", endpoint.transport}, {"socket_path", endpoint.socket_path},
                                              {"host", endpoint.host}, {"port", endpoint.port}});
        }
    }

    const char* env_timeout = getenv("XDEBUG_WAVEFORM_IDLE_TIMEOUT_SEC");
    int idle_timeout = env_timeout ? atoi(env_timeout) : 1800;
    if (idle_timeout <= 0) idle_timeout = 1800;
    server_debug_log("server_main: idle_timeout_sec=%d", idle_timeout);
    xdebug_core::log_lifecycle_event("waveform", g_session_id, "server.idle_timeout_config", true,
                                     {{"idle_timeout_sec", idle_timeout}});
    time_t last_active = time(nullptr);
    bool idle_timeout_exit = false;
    bool quit_requested = false;

    // Accept loop
    while (true) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(g_srv_fd, &rfds);
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        int ready = select(g_srv_fd + 1, &rfds, nullptr, nullptr, &tv);
        if (ready < 0) continue;
        if (ready == 0) {
            if (time(nullptr) - last_active > idle_timeout) {
                idle_timeout_exit = true;
                server_debug_log("server_main: idle_timeout_exit idle_sec=%ld timeout_sec=%d",
                                  static_cast<long>(time(nullptr) - last_active),
                                  idle_timeout);
                xdebug_core::log_lifecycle_event("waveform", g_session_id, "server.idle_timeout_exit", true,
                                                 {{"idle_sec", static_cast<long>(time(nullptr) - last_active)},
                                                  {"timeout_sec", idle_timeout}});
                break;
            }
            continue;
        }

        int client_fd = accept(g_srv_fd, nullptr, nullptr);
        if (client_fd < 0) continue;

        bool quit = false;
        handle_client(client_fd, quit);
        close(client_fd);
        last_active = time(nullptr);

        if (quit) {
            quit_requested = true;
            server_debug_log("server_main: quit_requested");
            xdebug_core::log_lifecycle_event("waveform", g_session_id, "server.quit_requested", true);
            break;
        }
    }

    // Cleanup
    server_debug_log("server_main: cleanup_begin reason=%s",
                     idle_timeout_exit ? "idle_timeout" :
                     (quit_requested ? "quit" : "loop_exit"));
    xdebug_core::log_lifecycle_event("waveform", g_session_id, "server.cleanup_begin", true,
                                     {{"reason", idle_timeout_exit ? "idle_timeout" : (quit_requested ? "quit" : "loop_exit")}});
    close(g_srv_fd);
    unlink(g_sock_path);
    if (g_fsdb_file) {
        npi_fsdb_close(g_fsdb_file);
        g_fsdb_file = nullptr;
    }
    {
        SessionRegistry registry;
        registry.remove(g_session_id);
    }
    npi_end();
    if (g_debug_log) {
        server_debug_log("server_main: normal_exit");
        xdebug_core::log_lifecycle_event("waveform", g_session_id, "server.normal_exit", true);
        fclose(g_debug_log);
        g_debug_log = nullptr;
    }

    return 0;
}

} // namespace xdebug_waveform
