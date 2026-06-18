#pragma once

#include "api/json_types.h"

#include <string>
#include <vector>

namespace xdebug {

struct SessionRecord {
    std::string id;
    std::string mode;
    std::string daidir;
    std::string fsdb;
    std::string socket_path;
};

// Read-only view of the canonical engine registry.
// Session lifecycle mutations are owned by the engine SessionRegistry.
class SessionCatalog {
public:
    SessionCatalog();

    bool get(const std::string& id, SessionRecord& record) const;
    std::vector<SessionRecord> list() const;

private:
    std::string path_;
    Json read_all() const;
    static bool parse_record(const Json& item, SessionRecord& record);
};

Json session_record_json(const SessionRecord& record);

} // namespace xdebug
