VCS              ?= vcs
VERDI_HOME       ?= /home/yian/Synopsys/verdi/V-2023.12-SP2
NOVAS_TAB        := $(VERDI_HOME)/share/PLI/VCS/linux64/novas.tab
NOVAS_PLI        := $(VERDI_HOME)/share/PLI/VCS/linux64/pli.a
SIMV             := simv
TEST             ?= mode10_master_slave_agent_pairing_test
XWAVE            ?= /home/yian/xwave/xwave

SV_SRCS          := $(wildcard ../../src/*.sv *.sv)

VCS_FLAGS := \
	-full64 \
	-sverilog \
	-ntb_opts uvm-1.2 \
	-timescale=1ns/1ps \
	-debug_access+all \
	-kdb \
	-lca \
	+vcs+lic+wait \
	+incdir+../../src \
	+incdir+. \
	-P $(NOVAS_TAB) $(NOVAS_PLI) \
	-f filelist.f \
	-o $(SIMV)

RUN_FLAGS := +UVM_TESTNAME=$(TEST)

.PHONY: all sim run clean

all: sim

sim: $(SIMV)

$(SIMV): filelist.f $(SV_SRCS)
	@test -f "$(NOVAS_TAB)"
	@test -f "$(NOVAS_PLI)"
	@mkdir -p logs waves
	$(VCS) $(VCS_FLAGS) -l logs/compile.log

run: $(SIMV)
	@mkdir -p logs waves
	./$(SIMV) $(RUN_FLAGS) -l logs/$(TEST).log

clean:
	rm -rf simv simv.daidir csrc ucli.key logs waves novas* verdiLog .vcs_lib_lock
