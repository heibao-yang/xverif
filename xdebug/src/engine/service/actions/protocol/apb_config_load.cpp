#include "service/engine_action_handler.h"
#include "service/engine_action_registry.h"
#include "service/engine_globals.h"

#include "waveform/apb/apb_manager.h"
#include "waveform/apb/apb_analyzer.h"
#include "waveform/axi/axi_manager.h"
#include "waveform/axi/axi_analyzer.h"
#include "waveform/axi/axi_exporter.h"
#include "waveform/common/xdebug_waveform_paths.h"
#include "waveform/value/logic_value.h"

#include <fstream>
#include <memory>
#include <ctime>
#include <sstream>

namespace xdebug_design {
namespace {
class ApbConfigLoadHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "apb.config.load"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& r, EngineActionContext& ctx) const override {
        using namespace xdebug_waveform;
        Json a = r.value("args", Json::object());
        std::string name = a.value("name", "");
        if (name.empty()) return Json({{"error","MISSING_FIELD"},{"message","args.name"}});

        nlohmann::json cfg_j; std::string err_str;
        if (!load_config_from_args(a, cfg_j, err_str))
            return Json({{"error","INVALID_REQUEST"},{"message",err_str}});

        const char* legacy[] = {"clk", "sampling", "clock_edge", "posedge", "sample_offset", nullptr};
        for (int i = 0; legacy[i]; ++i) {
            if (cfg_j.contains(legacy[i])) {
                return Json({{"error","INVALID_REQUEST"},
                             {"invalid_arg", std::string("config.") + legacy[i]},
                             {"message","legacy clock sampling field is not supported; use config.clock, config.edge, and config.sample_point"}});
            }
        }
        const char* reqs[] = {"clock","rst_n","paddr","psel","penable","pwrite","pwdata","prdata",nullptr};
        for (int i = 0; reqs[i]; ++i) {
            if (!cfg_j.contains(reqs[i]) || !cfg_j[reqs[i]].is_string() ||
                cfg_j[reqs[i]].get<std::string>().empty())
                return Json({{"error","INVALID_REQUEST"},
                    {"message",std::string("missing or empty field: ")+reqs[i]}});
        }

        ApbConfig cfg;
        cfg.name = name;
        cfg.clock_sample.clock = cfg_j["clock"].get<std::string>();
        if (!parse_clock_edge_kind(cfg_j.value("edge", std::string("negedge")),
                                   cfg.clock_sample.edge,
                                   err_str)) {
            return Json({{"error","INVALID_REQUEST"},{"message",err_str}});
        }
        if (cfg_j.contains("sample_point")) {
            if (!cfg_j["sample_point"].is_string())
                return Json({{"error","INVALID_REQUEST"},{"message","config.sample_point must be before or after"}});
            cfg.clock_sample.has_sample_point = true;
            if (!parse_clock_sample_point_kind(cfg_j["sample_point"].get<std::string>(),
                                               cfg.clock_sample.sample_point,
                                               err_str))
                return Json({{"error","INVALID_REQUEST"},{"message",err_str}});
        }
        if (cfg.clock_sample.edge == ClockEdgeKind::Negedge &&
            cfg.clock_sample.has_sample_point)
            return Json({{"error","INVALID_REQUEST"},{"message","config.sample_point is only valid with edge:posedge or edge:dual"}});
        cfg.rst_n = cfg_j["rst_n"].get<std::string>();
        cfg.paddr = cfg_j["paddr"].get<std::string>();
        cfg.psel = cfg_j["psel"].get<std::string>();
        cfg.penable = cfg_j["penable"].get<std::string>();
        cfg.pwrite = cfg_j["pwrite"].get<std::string>();
        cfg.pwdata = cfg_j["pwdata"].get<std::string>();
        cfg.prdata = cfg_j["prdata"].get<std::string>();
        cfg.pready = cfg_j.value("pready", "");
        cfg.pslverr = cfg_j.value("pslverr", "");

        ApbManager am;
        if (!am.create_apb(g_session_id, cfg))
            return Json({{"error","CREATE_FAILED"},{"message","failed to save APB config"}});

        Json out;
        out["summary"] = {{"name", name}, {"status", "loaded"}};
        out["name"] = name; out["status"] = "loaded";
        Json cinfo;
        cinfo["name"] = name;
        cinfo["sampling_mode"] = "clock_edge";
        cinfo["clock"] = cfg.clock_sample.clock;
        cinfo["edge"] = clock_edge_kind_text(cfg.clock_sample.edge);
        if (cfg.clock_sample.edge != ClockEdgeKind::Negedge)
            cinfo["sample_point"] = clock_sample_point_text(cfg.clock_sample.sample_point);
        cinfo["rst_n"] = cfg.rst_n;
        if (!cfg.pready.empty()) cinfo["pready"] = cfg.pready;
        if (!cfg.pslverr.empty()) cinfo["pslverr"] = cfg.pslverr;
        out["config"] = cinfo;
        return out;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_apb_config_load_handler() {
    return std::unique_ptr<EngineActionHandler>(new ApbConfigLoadHandler);
}

}  // namespace xdebug_design
