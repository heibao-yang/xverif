#include "session/session_catalog.h"
#include "test_temp_path.h"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

int main() {
    std::vector<char> temp = test_temp_template("xdebug-session-catalog.XXXXXX");
    char* home = mkdtemp(temp.data());
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
      "fsdb_file": "fixtures/waves.fsdb",
      "socket_path": "fixtures/wave.sock",
      "server_pid": 123,
      "created_at": 1000,
      "last_active": 1200
    },
    {
      "session_id": "design",
      "dbdir_path": "fixtures/simv.daidir",
      "socket_path": "fixtures/design.sock"
    },
    {
      "session_id": "combined",
      "dbdir_path": "fixtures/simv.daidir",
      "fsdb_file": "fixtures/waves.fsdb"
    }
  ]
})JSON";
    canonical.close();

    // A stale legacy frontend registry must not shadow canonical engine state.
    std::ofstream legacy((xdebug_home + "/registry.json").c_str());
    legacy << R"JSON([{"id":"stale","mode":"waveform","fsdb":"fixtures/stale.fsdb"}])JSON";
    legacy.close();

    xdebug::SessionCatalog catalog;
    std::vector<xdebug::SessionRecord> records = catalog.list();
    assert(records.size() == 3);

    xdebug::SessionRecord record;
    assert(catalog.get("wave", record));
    assert(record.mode == "waveform");
    assert(record.fsdb == "fixtures/waves.fsdb");
    assert(record.socket_path == "fixtures/wave.sock");
    assert(record.server_pid == 123);
    assert(record.created_at == 1000);
    assert(record.last_active == 1200);
    xdebug::Json wave_json = xdebug::session_record_json(record);
    assert(wave_json["server_pid"].get<int>() == 123);
    assert(wave_json["created_at"].get<long long>() == 1000);
    assert(wave_json["last_active"].get<long long>() == 1200);

    assert(catalog.get("design", record));
    assert(record.mode == "design");
    assert(record.daidir == "fixtures/simv.daidir");

    assert(catalog.get("combined", record));
    assert(record.mode == "combined");
    assert(!catalog.get("stale", record));

    // Corrupt registry content must behave like an empty registry rather than
    // surfacing partial data or crashing target resolution.
    std::ofstream corrupt((engine_home + "/registry.json").c_str());
    corrupt << R"JSON({"sessions": [)JSON";
    corrupt.close();
    assert(catalog.list().empty());
    assert(!catalog.get("wave", record));

    // Invalid records are skipped individually. A valid record after malformed
    // or incomplete entries must still be discoverable.
    std::ofstream mixed((engine_home + "/registry.json").c_str());
    mixed << R"JSON({
  "sessions": [
    "not-an-object",
    {"session_id": "", "fsdb_file": "fixtures/missing-id.fsdb"},
    {"session_id": "missing-resource"},
    {"session_id": "legacy-design-file", "design_file": "fixtures/legacy.daidir"},
    {
      "session_id": "file-transport",
      "fsdb_file": "fixtures/file.fsdb",
      "transport": "file",
      "file_dir": "fixtures/xdebug-file-transport"
    },
    {
      "session_id": "tcp-transport",
      "dbdir_path": "fixtures/tcp.daidir",
      "transport": "tcp",
      "host": "launcher",
      "bind_host": "127.0.0.1",
      "port": 43123,
      "server_host": "worker"
    }
  ]
})JSON";
    mixed.close();
    records = catalog.list();
    assert(records.size() == 3);
    assert(!catalog.get("missing-resource", record));

    assert(catalog.get("legacy-design-file", record));
    assert(record.mode == "design");
    assert(record.daidir == "fixtures/legacy.daidir");
    assert(record.transport == "uds");

    assert(catalog.get("file-transport", record));
    assert(record.mode == "waveform");
    assert(record.transport == "file");
    assert(record.file_dir == "fixtures/xdebug-file-transport");

    assert(catalog.get("tcp-transport", record));
    assert(record.mode == "design");
    assert(record.transport == "tcp");
    assert(record.host == "launcher");
    assert(record.bind_host == "127.0.0.1");
    assert(record.port == 43123);
    assert(record.server_host == "worker");
    return 0;
}
