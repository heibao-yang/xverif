#include "waveform/cache/analysis_probe.h"

#include "json.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using xdebug_waveform::AnalysisProbe;
using xdebug_waveform::AnalysisProbeMetrics;
using Json = nlohmann::ordered_json;

int main() {
    char path[] = "/tmp/xdebug-analysis-probe.XXXXXX";
    const int fd = mkstemp(path);
    assert(fd >= 0);
    assert(fchmod(fd, 0644) == 0);
    close(fd);

    AnalysisProbe probe(path);
    assert(probe.enabled());
    probe.record("miss", "stream", "config-a",
                 AnalysisProbeMetrics{0, 0, 0, 0, 0});
    probe.record("scan", "stream", "config-a",
                 AnalysisProbeMetrics{0, 0, 0, 4096, 1});
    probe.record("build", "stream", "config-a",
                 AnalysisProbeMetrics{1, 0, 2048, 4096, 0});
    probe.record("hit", "stream", "config-a",
                 AnalysisProbeMetrics{1, 0, 2048, 0, 0});

    std::ifstream input(path);
    std::vector<Json> rows;
    std::string line;
    while (std::getline(input, line)) rows.push_back(Json::parse(line));
    assert(rows.size() == 4);
    assert(rows[0]["schema"] == "xdebug.analysis-probe.v1");
    assert(rows[0]["event"] == "miss");
    assert(rows[0]["misses"] == 1);
    assert(rows[1]["scanner_invocations"] == 1);
    assert(rows[2]["entry_count"] == 1);
    assert(rows[2]["resident_bytes"] == 2048);
    assert(rows[3]["hits"] == 1);
    assert(rows[3]["access_sequence"] == 4);
    assert(rows[0]["key_summary"] == rows[3]["key_summary"]);
    assert(rows[0]["key_summary"] != "config-a");

    struct stat info {};
    assert(stat(path, &info) == 0);
    assert((info.st_mode & 0777) == 0600);
    std::remove(path);
    return 0;
}
