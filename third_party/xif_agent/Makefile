VCS              ?= vcs
VERDI_HOME       ?= /home/yian/Synopsys/verdi/V-2023.12-SP2
NOVAS_TAB        := $(VERDI_HOME)/share/PLI/VCS/linux64/novas.tab
NOVAS_PLI        := $(VERDI_HOME)/share/PLI/VCS/linux64/pli.a
SIMV             := simv
TEST             ?= xif_master_rdy_test
SMOKE_TESTS      := xif_master_rdy_test xif_master_bp_test xif_master_none_test xif_slave_responder_test xif_passive_monitor_test xif_master_duplicate_pd_test
COMPLEX_TESTS    := xif_back_to_back_sequence_test xif_burst_sequence_test xif_pulse_sequence_test xif_multi_channel_outstanding_test xif_multi_channel_weighted_arb_test xif_driver_mailbox_prefetch_test xif_reaction_sequence_test xif_complex_integrated_test xif_master_slave_dual_test
XWAVE            ?= /home/yian/xwave/xwave
XDEBUG           ?= /home/yian/xverif/tools/xdebug
SV_SRCS          := $(wildcard src/*.sv tb/*.sv)
SVH_SRCS         := $(wildcard src/*.svh tb/*.svh)

VCS_FLAGS := \
	-full64 \
	-sverilog \
	-ntb_opts uvm-1.2 \
	-timescale=1ns/1ps \
	-debug_access+all \
	-kdb \
	-lca \
	+incdir+$(CURDIR)/src \
	+incdir+$(CURDIR)/tb \
	-P $(NOVAS_TAB) $(NOVAS_PLI) \
	-f filelist.f \
	-o $(SIMV)

RUN_FLAGS := +UVM_TESTNAME=$(TEST)

.PHONY: all sim run smoke complex clean check-wave xdebug-stream-report

all: sim

sim: $(SIMV)

$(SIMV): filelist.f $(SV_SRCS) $(SVH_SRCS)
	@test -f "$(NOVAS_TAB)"
	@test -f "$(NOVAS_PLI)"
	@mkdir -p build logs waves
	$(VCS) $(VCS_FLAGS) -l build/compile.log

run: $(SIMV)
	@mkdir -p logs waves
	./$(SIMV) $(RUN_FLAGS) -l logs/$(TEST).log

smoke: $(SIMV)
	@mkdir -p logs waves
	@for test in $(SMOKE_TESTS); do \
		./$(SIMV) +UVM_TESTNAME=$$test -l logs/$$test.log || exit $$?; \
	done

complex: $(SIMV)
	@mkdir -p logs waves
	@for test in $(COMPLEX_TESTS); do \
		./$(SIMV) +UVM_TESTNAME=$$test -l logs/$$test.log || exit $$?; \
	done

check-wave:
	@python3 scripts/check_waves.py --test $(TEST) --xwave $(XWAVE)

xdebug-stream-report:
	@python3 scripts/xdebug_stream_report.py --xdebug $(XDEBUG)

clean:
	rm -rf build csrc simv simv.daidir ucli.key logs waves reports novas* verdiLog .vcs_lib_lock
