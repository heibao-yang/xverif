#include "session/session_catalog.h"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

int main() {
    char temp[] = "/tmp/xdebug-session-catalog.XXXXXX";
    char* home = mkdtemp(temp);
    assert(home != nullptr);
    assert(setenv("HOME", home, 1) == 0);

    const std::string xdebug_home = std::string(home) + "/.xdebug";
    const std::string engine_home = xdebug_home + "/engine";
    assert(mkdir(xdebug_home.c_str(), 0700) == 0);
    assert(mkdir(engine_home.c_str(), 0700) == 0);

    std::ofstream canonical((engine_home + "/registry.json").c_str());
    canonical << R"JSON({
  "version": 1,
  "sessions": [
    {
      "session_id": "wave",
      "fsdb_file": "/tmp/waves.fsdb",
      "socket_path": "/tmp/wave.sock"
    },
    {
      "session_id": "design",
      "dbdir_path": "/tmp/simv.daidir",
      "socket_path": "/tmp/design.sock"
    },
    {
      "session_id": "combined",
      "dbdir_path": "/tmp/simv.daidir",
      "fsdb_file": "/tmp/waves.fsdb"
    }
  ]
})JSON";
    canonical.close();

    // A stale legacy frontend registry must not shadow canonical engine state.
    std::ofstream legacy((xdebug_home + "/registry.json").c_str());
    legacy << R"JSON([{"id":"stale","mode":"waveform","fsdb":"/tmp/stale.fsdb"}])JSON";
    legacy.close();

    xdebug::SessionCatalog catalog;
    std::vector<xdebug::SessionRecord> records = catalog.list();
    assert(records.size() == 3);

    xdebug::SessionRecord record;
    assert(catalog.get("wave", record));
    assert(record.mode == "waveform");
    assert(record.fsdb == "/tmp/waves.fsdb");
    assert(record.socket_path == "/tmp/wave.sock");

    assert(catalog.get("design", record));
    assert(record.mode == "design");
    assert(record.daidir == "/tmp/simv.daidir");

    assert(catalog.get("combined", record));
    assert(record.mode == "combined");
    assert(!catalog.get("stale", record));
    return 0;
}
