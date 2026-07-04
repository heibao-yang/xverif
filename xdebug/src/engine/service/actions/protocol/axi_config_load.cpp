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
class AxiConfigLoadHandler : public EngineActionHandler {
public:
    const char* action_name() const override { return "axi.config.load"; }
    bool needs_design() const override { return false; }
    bool needs_waveform() const override { return true; }
    Json run(const Json& r, EngineActionContext& ctx) const override {
        using namespace xdebug_waveform;
        Json a = r.value("args", Json::object());
        std::string name = a.value("name", "");
        if (name.empty()) return Json({{"error","MISSING_FIELD"},{"message","args.name"}});

        nlohmann::json cfg_j; std::string err;
        if (!load_config_from_args(a, cfg_j, err))
            return Json({{"error","INVALID_REQUEST"},{"message",err}});

        const char* legacy[] = {"clk", "sampling", "clock_edge", "posedge", nullptr};
        for (int i = 0; legacy[i]; ++i) {
            if (cfg_j.contains(legacy[i])) {
                return Json({{"error","INVALID_REQUEST"},
                             {"invalid_arg", std::string("config.") + legacy[i]},
                             {"message","legacy clock sampling field is not supported; use config.clock, config.edge, and config.sample_offset"}});
            }
        }

        // Validate required AXI fields
        const char* reqs[] = {"clock","rst_n",
            "awvalid","awready","awaddr","awid","awlen","awsize","awburst",
            "wvalid","wready","wdata","wstrb","wlast",
            "bvalid","bready","bid","bresp",
            "arvalid","arready","araddr","arid","arlen","arsize","arburst",
            "rvalid","rready","rdata","rid","rresp","rlast",nullptr};
        for (int i = 0; reqs[i]; ++i) {
            if (!cfg_j.contains(reqs[i]) || !cfg_j[reqs[i]].is_string() ||
                cfg_j[reqs[i]].get<std::string>().empty())
                return Json({{"error","INVALID_REQUEST"},
                    {"message",std::string("missing or empty field: ")+reqs[i]}});
        }

        AxiConfig cfg; cfg.name = name;
        cfg.clock_sample.clock = cfg_j["clock"].get<std::string>();
        if (!parse_clock_edge_kind(cfg_j.value("edge", std::string("negedge")),
                                   cfg.clock_sample.edge,
                                   err)) {
            return Json({{"error","INVALID_REQUEST"},{"message",err}});
        }
        cfg.clock_sample.sample_offset_text = cfg_j.value("sample_offset", std::string("0ns"));
        cfg.rst_n = cfg_j["rst_n"].get<std::string>();
        cfg.awvalid=cfg_j["awvalid"]; cfg.awready=cfg_j["awready"];
        cfg.awaddr=cfg_j["awaddr"]; cfg.awid=cfg_j["awid"];
        cfg.awlen=cfg_j["awlen"]; cfg.awsize=cfg_j["awsize"]; cfg.awburst=cfg_j["awburst"];
        cfg.wvalid=cfg_j["wvalid"]; cfg.wready=cfg_j["wready"];
        cfg.wdata=cfg_j["wdata"]; cfg.wstrb=cfg_j["wstrb"]; cfg.wlast=cfg_j["wlast"];
        cfg.bvalid=cfg_j["bvalid"]; cfg.bready=cfg_j["bready"];
        cfg.bid=cfg_j["bid"]; cfg.bresp=cfg_j["bresp"];
        cfg.arvalid=cfg_j["arvalid"]; cfg.arready=cfg_j["arready"];
        cfg.araddr=cfg_j["araddr"]; cfg.arid=cfg_j["arid"];
        cfg.arlen=cfg_j["arlen"]; cfg.arsize=cfg_j["arsize"]; cfg.arburst=cfg_j["arburst"];
        cfg.rvalid=cfg_j["rvalid"]; cfg.rready=cfg_j["rready"];
        cfg.rdata=cfg_j["rdata"]; cfg.rid=cfg_j["rid"];
        cfg.rresp=cfg_j["rresp"]; cfg.rlast=cfg_j["rlast"];

        AxiManager am;
        if (!am.create_axi(g_session_id, cfg))
            return Json({{"error","CREATE_FAILED"},{"message","failed to save AXI config"}});

        Json out;
        out["summary"] = {{"name", name}, {"status", "loaded"}};
        out["name"] = name; out["status"] = "loaded";
        Json cinfo;
        cinfo["name"] = name;
        cinfo["sampling_mode"] = "clock_edge";
        cinfo["clock"] = cfg.clock_sample.clock;
        cinfo["edge"] = clock_edge_kind_text(cfg.clock_sample.edge);
        cinfo["sample_offset"] = cfg.clock_sample.sample_offset_text.empty()
            ? "0ns" : cfg.clock_sample.sample_offset_text;
        cinfo["rst_n"] = cfg.rst_n;
        out["config"] = cinfo;
        return out;
    }
};

}  // namespace

std::unique_ptr<EngineActionHandler> make_axi_config_load_handler() {
    return std::unique_ptr<EngineActionHandler>(new AxiConfigLoadHandler);
}

}  // namespace xdebug_design
