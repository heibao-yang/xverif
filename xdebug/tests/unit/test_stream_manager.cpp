#include "waveform/common/xdebug_waveform_paths.h"
#include "waveform/stream/stream_manager.h"

#include <cassert>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace xdebug_waveform;

namespace {

StreamConfig config(const std::string& name, const std::string& signal,
                    const std::string& description) {
    StreamConfig value;
    value.name = name;
    value.signals["clk"] = "top.clk";
    value.signals["vld"] = "top.vld";
    value.signals["data"] = signal;
    value.clock_sample.clock = "clk";
    value.vld = "vld";
    value.data = "data";
    value.description = description;
    return value;
}

std::string read_text(const std::string& path) {
    std::ifstream input(path.c_str());
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

bool has_temporary(const std::string& directory) {
    DIR* dir = opendir(directory.c_str());
    assert(dir != nullptr);
    bool found = false;
    while (dirent* entry = readdir(dir)) {
        if (std::string(entry->d_name).find("streams.json.tmp.") == 0) {
            found = true;
            break;
        }
    }
    closedir(dir);
    return found;
}

}  // namespace

int main() {
    char root[] = "/tmp/xdebug-stream-manager.XXXXXX";
    assert(mkdtemp(root) != nullptr);
    setenv("HOME", root, 1);
    const std::string session = "StreamAtomic";
    StreamManager manager;
    std::string error;
    std::vector<StreamConfigChange> changes;

    const StreamConfig original = config("stream0", "top.data_a", "old text");
    assert(manager.load_configs(session, {original}, "replace", error, &changes));
    assert(changes.size() == 1 && changes[0].old_semantic_fingerprint.empty());
    const std::string path = xdebug_waveform_streams_path(session);
    const std::string directory = xdebug_waveform_session_dir(session);
    const std::string before = read_text(path);
    assert(!before.empty());
    struct stat info {};
    assert(stat(path.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600);
    assert(!has_temporary(directory));

    StreamConfig description_only = original;
    description_only.name = "another_name";
    description_only.description = "new text";
    assert(normalized_stream_config_semantics(original) ==
           normalized_stream_config_semantics(description_only));
    assert(stream_config_semantic_fingerprint(original) ==
           stream_config_semantic_fingerprint(description_only));

    const StreamConfig changed = config("stream0", "top.data_b", "changed");
    setenv("XDEBUG_TEST_STREAM_CONFIG_WRITE_FAIL", "1", 1);
    assert(!manager.load_configs(session, {changed}, "replace", error, &changes));
    unsetenv("XDEBUG_TEST_STREAM_CONFIG_WRITE_FAIL");
    assert(changes.empty());
    assert(read_text(path) == before);
    assert(!has_temporary(directory));

    setenv("XDEBUG_TEST_STREAM_CONFIG_RENAME_FAIL", "1", 1);
    assert(!manager.load_configs(session, {changed}, "replace", error, &changes));
    unsetenv("XDEBUG_TEST_STREAM_CONFIG_RENAME_FAIL");
    assert(changes.empty());
    assert(read_text(path) == before);
    assert(!has_temporary(directory));

    assert(manager.load_configs(session, {changed}, "replace", error, &changes));
    assert(changes.size() == 1);
    assert(changes[0].old_semantic_fingerprint ==
           stream_config_semantic_fingerprint(original));
    assert(changes[0].new_semantic_fingerprint ==
           stream_config_semantic_fingerprint(changed));
    assert(changes[0].old_semantic_fingerprint !=
           changes[0].new_semantic_fingerprint);
    assert(read_text(path) != before);
    StreamConfig loaded;
    assert(manager.get_stream(session, "stream0", loaded));
    assert(loaded.signals.at("data") == "top.data_b");
    assert(!has_temporary(directory));

    xdebug_waveform_remove_session_dir(session);
    return 0;
}
