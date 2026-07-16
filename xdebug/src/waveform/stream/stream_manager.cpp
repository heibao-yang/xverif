#include "stream_manager.h"

#include "../common/xdebug_waveform_paths.h"

#include <fcntl.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace xdebug_waveform {

namespace {

bool lock_file(int fd) {
    return flock(fd, LOCK_EX) == 0;
}

bool lock_file_shared(int fd) {
    return flock(fd, LOCK_SH) == 0;
}

void unlock_file(int fd) {
    flock(fd, LOCK_UN);
}

Json storage_json(const std::vector<StreamConfig>& configs) {
    Json root;
    root["version"] = 1;
    root["streams"] = Json::array();
    for (const auto& config : configs) root["streams"].push_back(stream_config_json(config));
    return root;
}

std::string parent_dir(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    if (slash == std::string::npos) return ".";
    return slash == 0 ? "/" : path.substr(0, slash);
}

bool write_all(int fd, const std::string& data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t count = write(fd, data.data() + offset, data.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        offset += static_cast<std::size_t>(count);
    }
    return true;
}

bool test_hook_enabled(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && std::strcmp(value, "1") == 0;
}

} // namespace

bool load_stream_config_arg(const Json& args, Json& root, std::string& error) {
    if (args.contains("streams")) {
        if (!args["streams"].is_array()) {
            error = "args.streams must be an array";
            return false;
        }
        root = Json{{"streams", args["streams"]}};
        return true;
    }
    if (args.contains("config")) {
        if (!args["config"].is_object()) {
            error = "args.config must be an object";
            return false;
        }
        root = args["config"];
        return true;
    }
    std::string path = args.value("config_path", args.value("file", std::string()));
    if (path.empty()) {
        error = "stream.config.load requires args.streams, args.config, args.config_path, or args.file";
        return false;
    }
    std::ifstream input(path.c_str());
    if (!input) {
        error = "config file not found: " + path;
        return false;
    }
    try {
        input >> root;
    } catch (const std::exception& e) {
        error = std::string("invalid JSON in stream config file: ") + e.what();
        return false;
    }
    if (!root.is_object()) {
        error = "stream config file must contain a JSON object";
        return false;
    }
    return true;
}

bool parse_stream_config_list(const Json& root, std::vector<StreamConfig>& streams, std::string& error) {
    streams.clear();
    Json arr;
    if (root.is_array()) arr = root;
    else if (root.is_object() && root.contains("streams")) arr = root["streams"];
    else {
        error = "stream config requires a streams array";
        return false;
    }
    if (!arr.is_array() || arr.empty()) {
        error = "stream config streams must be a non-empty array";
        return false;
    }
    std::set<std::string> names;
    for (const auto& item : arr) {
        StreamConfig config;
        if (!parse_stream_config_json(item, config, error)) return false;
        if (!names.insert(config.name).second) {
            error = "duplicate stream name in config: " + config.name;
            return false;
        }
        streams.push_back(config);
    }
    return true;
}

bool StreamManager::load_session(const std::string& session_id, std::vector<StreamConfig>& configs) {
    configs.clear();
    const std::string path = xdebug_waveform_streams_path(session_id);
    if (access(path.c_str(), F_OK) != 0) return true;
    const std::string lock_path = path + ".lock";
    int lock_fd = open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lock_fd < 0 || !lock_file_shared(lock_fd)) {
        if (lock_fd >= 0) close(lock_fd);
        return false;
    }
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        unlock_file(lock_fd);
        close(lock_fd);
        return false;
    }
    FILE* fp = fdopen(fd, "r");
    if (!fp) {
        close(fd);
        unlock_file(lock_fd);
        close(lock_fd);
        return false;
    }
    std::string text;
    char buf[4096];
    while (fgets(buf, sizeof(buf), fp)) text += buf;
    fclose(fp);
    unlock_file(lock_fd);
    close(lock_fd);
    if (text.empty()) return true;
    try {
        Json root = Json::parse(text);
        std::string error;
        return parse_stream_config_list(root, configs, error);
    } catch (...) {
        return false;
    }
}

bool StreamManager::save_session(const std::string& session_id, const std::vector<StreamConfig>& configs) {
    if (!xdebug_waveform_ensure_session_dir(session_id)) return false;
    const std::string path = xdebug_waveform_streams_path(session_id);
    const std::string lock_path = path + ".lock";
    int lock_fd = open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lock_fd < 0 || !lock_file(lock_fd)) {
        if (lock_fd >= 0) close(lock_fd);
        return false;
    }
    std::string temporary = path + ".tmp.XXXXXX";
    std::vector<char> temporary_buffer(temporary.begin(), temporary.end());
    temporary_buffer.push_back('\0');
    int fd = mkstemp(temporary_buffer.data());
    if (fd < 0) {
        unlock_file(lock_fd);
        close(lock_fd);
        return false;
    }
    temporary.assign(temporary_buffer.data());
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    bool ok = fchmod(fd, 0600) == 0;
    std::string data = storage_json(configs).dump(2) + "\n";
    if (test_hook_enabled("XDEBUG_TEST_STREAM_CONFIG_WRITE_FAIL")) ok = false;
    if (ok) ok = write_all(fd, data);
    if (ok) ok = fsync(fd) == 0;
    if (close(fd) != 0) ok = false;
    if (ok && test_hook_enabled("XDEBUG_TEST_STREAM_CONFIG_RENAME_FAIL")) ok = false;
    if (ok) ok = rename(temporary.c_str(), path.c_str()) == 0;
    if (ok) {
        const std::string directory = parent_dir(path);
        const int dir_fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dir_fd < 0) ok = false;
        else {
            ok = fsync(dir_fd) == 0;
            close(dir_fd);
        }
    }
    if (!ok) unlink(temporary.c_str());
    unlock_file(lock_fd);
    close(lock_fd);
    return ok;
}

bool StreamManager::load_configs(const std::string& session_id, const std::vector<StreamConfig>& incoming,
                                 const std::string& mode, std::string& error,
                                 std::vector<StreamConfigChange>* changes) {
    error.clear();
    if (changes != nullptr) changes->clear();
    if (mode != "replace" && mode != "append") {
        error = "stream.config.load mode must be replace or append";
        return false;
    }
    std::vector<StreamConfig> current;
    if (!load_session(session_id, current)) {
        error = "failed to load existing stream config";
        return false;
    }
    if (mode == "append") {
        std::set<std::string> names;
        for (const auto& item : current) names.insert(item.name);
        for (const auto& item : incoming) {
            if (!names.insert(item.name).second) {
                error = "stream already exists: " + item.name;
                return false;
            }
            current.push_back(item);
        }
        if (!save_session(session_id, current)) {
            error = "failed to atomically persist stream configs";
            return false;
        }
        if (changes != nullptr) {
            const std::size_t existing_count = current.size() - incoming.size();
            for (std::size_t i = 0; i < current.size(); ++i) {
                const auto& item = current[i];
                StreamConfigChange change;
                change.name = item.name;
                if (i < existing_count)
                    change.old_semantic_fingerprint =
                        stream_config_semantic_fingerprint(item);
                change.new_semantic_fingerprint = stream_config_semantic_fingerprint(item);
                changes->push_back(change);
            }
        }
        return true;
    }

    std::map<std::string, StreamConfig> by_name;
    for (const auto& item : current) by_name[item.name] = item;
    for (const auto& item : incoming) by_name[item.name] = item;
    std::vector<StreamConfig> out;
    for (const auto& kv : by_name) out.push_back(kv.second);
    if (!save_session(session_id, out)) {
        error = "failed to atomically persist stream configs";
        return false;
    }
    if (changes != nullptr) {
        std::map<std::string, StreamConfig> old_by_name;
        for (const auto& item : current) old_by_name[item.name] = item;
        for (const auto& item : out) {
            StreamConfigChange change;
            change.name = item.name;
            auto old = old_by_name.find(item.name);
            if (old != old_by_name.end())
                change.old_semantic_fingerprint =
                    stream_config_semantic_fingerprint(old->second);
            change.new_semantic_fingerprint = stream_config_semantic_fingerprint(item);
            changes->push_back(change);
        }
    }
    return true;
}

bool StreamManager::get_stream(const std::string& session_id, const std::string& name, StreamConfig& config) {
    std::vector<StreamConfig> configs;
    if (!load_session(session_id, configs)) return false;
    for (const auto& item : configs) {
        if (item.name == name) {
            config = item;
            return true;
        }
    }
    return false;
}

std::vector<StreamConfig> StreamManager::list_streams(const std::string& session_id) {
    std::vector<StreamConfig> configs;
    load_session(session_id, configs);
    return configs;
}

} // namespace xdebug_waveform
