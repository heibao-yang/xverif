# xif_agent v1 代码检阅报告

## 整体评价

项目完成度很高。四个 smoke test 全部 0 ERROR / 0 FATAL 通过，xwave 波形检查全部 PASS。架构清晰，参数化干净，codex_plan 中的核心需求基本都已落地。

以下按优先级列出可优化的点。

---

## 功能性问题

### 1. monitor 去重逻辑有隐患（高优先级）

`xif_monitor` 用 `prev_handshake && (curr_pd_bits === prev_pd_bits)` 过滤连续同 pd 的握手。如果 master 连续发两笔相同 pd 的 item，第二笔会被静默吞掉。

建议：去掉 monitor 内的去重，或改为在 scoreboard 层面做比对。

### 2. slave_responder_test 预期数量与实际不匹配

日志显示 monitor 发了 6 个 `XIF_MON_HS`，但 test 只 `wait_for_mon_items(4)` 就 drop objection，剩下 2 笔被丢弃。不算 fail，但说明 test 的预期数量和实际行为不对齐。

建议：将 `wait_for_mon_items` 的 count 与实际握手次数对齐，或在 test 结束时检查 fifo 是否为空。

### 3. `drv_cb` 的 `#1step` input 采样

`default input #1step output #0` 意味着 driver 读到的 `rdy/bp` 是上一拍末尾的值。在 RDY/BP 模式下，如果 responder 和 driver 在同一个 posedge 同时更新，可能出现一拍的采样偏差。

建议：如果这是有意设计，在代码中加注释说明；否则考虑统一采样策略。

### 4. `force`/`release` 与 responder 的冗余驱动

`set_flow_force` 用 `force` 强制信号，但 responder 的 `always @(posedge clk)` 在 `forced_state != XIF_FORCE_RLS` 时仍会调用 `drive_flow()`（只是被 force 覆盖）。功能上没问题，但 force 和 NBA 同时作用于同一信号在某些仿真器上可能产生 warning。

建议：在 responder 逻辑中，当 `forced_state != XIF_FORCE_RLS` 时直接 skip `drive_flow` 调用（当前已部分实现，但 force 侧仍在驱动，略冗余）。

---

## 结构性建议

### 5. `xif_cfg` 的包归属

`xif_cfg` 放在 `xif_pkg` 里，而 codex_plan 说 `xif_agent_pkg` 仅包含 class/typedef/enum。当前 `xif_pkg` 既有纯类型定义又有 UVM object，职责有点混。如果后续 `xif_pkg` 会被非 UVM 代码 import，可能会有问题。

建议：考虑将 `xif_cfg` 移到 `xif_agent_pkg`，保持 `xif_pkg` 的纯粹性。

### 6. `xif_env` 放在 `xif_agent_pkg` 里

env 通常是用户侧组件，不属于 agent 的可复用部分。

建议：将 `xif_env` 移到 `xif_tb_pkg` 或单独的 env_pkg，保持 agent_pkg 只包含 agent 本身的可复用组件。

### 7. 缺少 `passive` 模式的 smoke test

计划里提到 passive 只监控，但四个 test 全是 `UVM_ACTIVE`。

建议：补一个 passive monitor-only 的 case，验证 passive 模式下不创建 driver/sequencer 且 monitor 正常工作。

---

## 健壮性 / 可维护性

### 8. `sanitize()` 每拍调用

interface 的 responder always 块开头有 `cfg.sanitize()`，每个时钟沿都在做边界检查。性能上不是大问题，但语义上 sanitize 应该只在配置变更时调用一次。

建议：去掉 always 块中的 `sanitize()` 调用，仅在 `set_cfg()` 和 build_phase 中调用。

### 9. `xif_item` 缺少 `do_compare` / `do_copy`

只有 `convert2string`，没有重载比较和拷贝。虽然用了 `uvm_object_param_utils`，但 `PD_T` 是参数化的 struct，field macro 可能不会自动展开。

建议：如果后续加 scoreboard 做自动比对，需要手动实现 `do_compare` 和 `do_copy`。

### 10. `check_waves.py` 时间点硬编码

所有 expected value 的时间点都是硬编码的。如果 reset 周期或 clocking block 延迟有任何变化，所有值都要手动更新。

建议：当前阶段可接受，长期考虑从仿真 log 中提取时间戳或使用相对时间。

### 11. 缺少 SVA property（高优先级）

codex_plan 提到 interface 内要有 SVA 检查，目前只有 always 块里的 `uvm_report_error` 做运行时检查，没有用 `assert property` / `assume property`。

建议至少加以下几条：
- vld 稳定性：vld 拉高后在握手前不能撤
- pd 稳定性：pd 在 vld 有效期间不变（直到握手完成）
- reset 期间无有效握手

---

## 小问题

### 12. `mon_cb` 的 input skew

`mon_cb` 使用 `default input #0 output #0`，input skew 为 0 意味着采样的是当拍 posedge 的值。如果有 delta-cycle 竞争可能采到中间态。

建议：常见做法是 `input #1step`，与 `drv_cb` 保持一致。

### 13. `if_compile_only` 实例化

tb_top 里实例化了一个 `xif_alt_pd_t` 的 interface 和一堆 alt 类型变量，纯粹为了编译检查。

建议：用 `` `ifdef COMPILE_CHECK `` 包起来，避免仿真时多余的信号 dump 和资源占用。

---

## 实际运行验证结果

| 阶段 | 测试 | 结果 |
|------|------|------|
| VCS 仿真 | xif_master_rdy_test | 0 ERROR, 0 FATAL |
| VCS 仿真 | xif_master_bp_test | 0 ERROR, 0 FATAL |
| VCS 仿真 | xif_master_none_test | 0 ERROR, 0 FATAL |
| VCS 仿真 | xif_slave_responder_test | 0 ERROR, 0 FATAL |
| xwave 波形 | xif_master_rdy_test | PASS |
| xwave 波形 | xif_master_bp_test | PASS |
| xwave 波形 | xif_master_none_test | PASS |
| xwave 波形 | xif_slave_responder_test | PASS |

---

## 优先级总结

| 优先级 | 项 | 编号 |
|--------|----|------|
| 高 | monitor 去重逻辑 | #1 |
| 高 | 补充 SVA property | #11 |
| 中 | slave test 预期数量对齐 | #2 |
| 中 | xif_env 包归属 | #6 |
| 中 | 补 passive smoke test | #7 |
| 中 | xif_item do_compare/do_copy | #9 |
| 低 | sanitize 每拍调用 | #8 |
| 低 | drv_cb 采样说明 | #3 |
| 低 | force/release 冗余 | #4 |
| 低 | xif_cfg 包归属 | #5 |
| 低 | check_waves 硬编码 | #10 |
| 低 | mon_cb input skew | #12 |
| 低 | compile_only 宏保护 | #13 |
