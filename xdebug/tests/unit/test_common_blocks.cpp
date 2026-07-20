#include "core/ai/common_blocks.h"
#include "test_temp_path.h"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

using namespace xdebug;

namespace {

std::string write_temp_config(const std::string& content) {
    std::vector<char> path_storage = test_temp_template("xdebug_common_blocks.XXXXXX");
    char* path = path_storage.data();
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    std::ofstream out(path);
    out << content;
    out.close();
    return path;
}

} // namespace

int main() {
    unsetenv("XDEBUG_COMMON_BLOCKS");
    Json payload = {
        {"results", Json::array({Json{{"file", "rtl/common/fifo.sv"}, {"line", 12}}})}
    };
    Json original = payload;
    append_common_blocks_to_payload(payload);
    assert(payload == original);

    std::string config_path = write_temp_config(R"json({
      "schema_version": "xdebug.common_blocks.v1",
      "common_blocks": [
        {"file": "rtl/common/fifo.sv", "card": "docs/common/fifo.md"},
        {"file": "rtl/common/counter.sv", "card": "docs/common/counter.md"}
      ]
    })json");
    setenv("XDEBUG_COMMON_BLOCKS", config_path.c_str(), 1);

    payload = {
        {"results", Json::array({
            Json{{"file", "rtl/common/fifo.sv"}, {"line", 12}},
            Json{{"file", "rtl/common/fifo.sv"}, {"line", 18}},
            Json{{"file", "rtl/not_common/fifo.sv"}, {"line", 1}}
        })},
        {"nested", Json{{"edge", Json{{"file", "rtl/common/counter.sv"}}}}}
    };
    append_common_blocks_to_payload(payload);
    assert(payload.contains("common_blocks"));
    assert(payload["common_blocks"].is_array());
    assert(payload["common_blocks"].size() == 2);
    assert(payload["common_blocks"][0]["file"] == "rtl/common/fifo.sv");
    assert(payload["common_blocks"][0]["card"] == "docs/common/fifo.md");
    assert(payload["common_blocks"][0].contains("message"));
    assert(payload["common_blocks"][1]["file"] == "rtl/common/counter.sv");

    std::string bad_config_path = write_temp_config(R"json({"schema_version":"wrong","common_blocks":[]})json");
    setenv("XDEBUG_COMMON_BLOCKS", bad_config_path.c_str(), 1);
    payload = {{"file", "rtl/common/fifo.sv"}};
    append_common_blocks_to_payload(payload);
    assert(!payload.contains("common_blocks"));

    unsetenv("XDEBUG_COMMON_BLOCKS");
    unlink(config_path.c_str());
    unlink(bad_config_path.c_str());
    return 0;
}
