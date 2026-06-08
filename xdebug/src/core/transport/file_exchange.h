#pragma once

#include "json.hpp"

#include <string>

namespace xdebug_core {

using Json = nlohmann::json;

struct FileExchangeResult {
    bool ok = false;
    std::string request_id;
    std::string status;
    std::string message;
    long elapsed_ms = 0;
    Json response;
};

std::string file_transport_dir(const std::string& session_dir);

bool ensure_file_transport_layout(const std::string& dir);

bool atomic_write_json_file(const std::string& path, const Json& payload);

std::string make_file_request_id();

FileExchangeResult file_exchange_send_request(const std::string& dir,
                                               const Json& request,
                                               int timeout_ms = 2000);

bool file_exchange_claim_one(const std::string& dir,
                             const std::string& agent_id,
                             std::string& request_id,
                             std::string& claim_path);

} // namespace xdebug_core
