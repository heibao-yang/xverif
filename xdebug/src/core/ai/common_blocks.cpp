#include "core/ai/common_blocks.h"

#include "common/env_config.h"

#include <fstream>
#include <map>
#include <set>
#include <string>

namespace xdebug {

namespace {

const char* kSchemaVersion = "xdebug.common_blocks.v1";
const char* kMessage =
    "This is a verified common block. Unless necessary, do not chase internal logic; "
    "use the summary card to continue reasoning.";

struct Registry {
    std::map<std::string, std::string> file_to_card;
};

Registry load_registry() {
    Registry registry;
    std::string path = xdebug_core::xdebug_common_blocks_path();
    if (path.empty()) return registry;

    std::ifstream in(path.c_str());
    if (!in) return registry;

    Json config;
    try {
        in >> config;
    } catch (...) {
        return registry;
    }
    if (!config.is_object()) return registry;
    if (config.value("schema_version", std::string()) != kSchemaVersion) return registry;

    Json blocks = config.value("common_blocks", Json::array());
    if (!blocks.is_array()) return registry;
    for (const auto& block : blocks) {
        if (!block.is_object()) continue;
        std::string file = block.value("file", std::string());
        std::string card = block.value("card", std::string());
        if (file.empty() || card.empty()) continue;
        registry.file_to_card[file] = card;
    }
    return registry;
}

void collect_matches(const Json& node,
                     const Registry& registry,
                     std::set<std::string>& emitted,
                     Json& matches) {
    if (node.is_object()) {
        auto file_it = node.find("file");
        if (file_it != node.end() && file_it->is_string()) {
            std::string file = file_it->get<std::string>();
            auto card_it = registry.file_to_card.find(file);
            if (card_it != registry.file_to_card.end() && emitted.insert(file).second) {
                matches.push_back({
                    {"message", kMessage},
                    {"file", file},
                    {"card", card_it->second}
                });
            }
        }
        for (auto it = node.begin(); it != node.end(); ++it) {
            collect_matches(it.value(), registry, emitted, matches);
        }
    } else if (node.is_array()) {
        for (const auto& item : node) {
            collect_matches(item, registry, emitted, matches);
        }
    }
}

} // namespace

void append_common_blocks_to_payload(Json& payload) {
    if (!payload.is_object()) return;
    Registry registry = load_registry();
    if (registry.file_to_card.empty()) return;

    Json matches = Json::array();
    std::set<std::string> emitted;
    collect_matches(payload, registry, emitted, matches);
    if (!matches.empty()) payload["common_blocks"] = matches;
}

} // namespace xdebug
