#include "service/trace_source_path_formatter.h"

#include <cassert>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

using Json = nlohmann::ordered_json;

std::string make_source_file() {
    char path[] = "/tmp/xdebug-trace-source-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    std::ofstream out(path);
    for (int i = 1; i <= 24; ++i) {
        out << "assign s" << i << " = in" << i << ";\n";
    }
    return path;
}

int count_substr(const std::string& text, const std::string& needle) {
    int count = 0;
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

Json path_item(const std::string& file, int line, Json signal_path) {
    return Json{{"file", file}, {"line", line}, {"signal_path", signal_path}};
}

} // namespace

int main() {
    std::string file = make_source_file();
    setenv("XDEBUG_TRACE_SOURCE_CONTEXT_LINES", "1", 1);
    setenv("XDEBUG_TRACE_SOURCE_MERGE_THRESHOLD_LINES", "10", 1);

    Json response = {
        {"summary", Json{{"signal", "top.out"}, {"mode", "load"}, {"path_count", 4}}},
        {"data", Json{{"paths", Json::array({
            path_item(file, 5, Json::array({"top.a", "top.b"})),
            path_item(file, 9, Json::array({"top.b", "top.c"})),
            path_item(file, 9, Json::array({"top.b", "top.c"})),
            path_item(file, 20, Json::array({"top.x", "top.y"})),
        })}}},
    };

    std::string text = xdebug_design::render_source_path_xout("trace.load", response);
    assert(text.find("@xdebug.trace.load.v1") == 0);
    assert(text.find("source: " + file + ":5-9") != std::string::npos);
    assert(text.find("source: " + file + ":20") != std::string::npos);
    assert(count_substr(text, "\nsource: ") == 2);
    assert(text.find(">   5 | assign s5 = in5;") != std::string::npos);
    assert(text.find(">   9 | assign s9 = in9;") != std::string::npos);
    assert(text.find("   10 | assign s10 = in10;") != std::string::npos);
    assert(text.find("   11 | assign s11 = in11;") == std::string::npos);
    assert(text.find("\nactive_signals:\n") != std::string::npos);
    assert(text.find("line  signal_path") != std::string::npos);
    assert(text.find("5     top.a -> top.b") != std::string::npos);
    assert(count_substr(text, "9     top.b -> top.c") == 1);
    assert(text.find("\nsignal_path: ") == std::string::npos);

    Json chain_response = {
        {"summary", Json{{"signal", "top.out"}, {"hop_count", 2}}},
        {"data", Json{{"hops", Json::array({
            Json{{"index", 1}, {"file", file}, {"line", 5}, {"signal_path", Json::array({"top.a", "top.b"})}},
            Json{{"index", 2}, {"file", file}, {"line", 9}, {"signal_path", Json::array({"top.b", "top.c"})}},
        })}}},
    };
    std::string chain_text = xdebug_design::render_source_path_xout("trace.active_driver_chain", chain_response);
    assert(chain_text.find("hop  line  signal_path") != std::string::npos);
    assert(chain_text.find("1    5     top.a -> top.b") != std::string::npos);
    assert(chain_text.find("2    9     top.b -> top.c") != std::string::npos);

    unlink(file.c_str());
    unsetenv("XDEBUG_TRACE_SOURCE_CONTEXT_LINES");
    unsetenv("XDEBUG_TRACE_SOURCE_MERGE_THRESHOLD_LINES");
    return 0;
}
