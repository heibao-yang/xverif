# 多通道 Sequence 激励控制计划

## Summary
- 在 TB sequence 层实现，不扩展 `xif_agent` / `xif_cfg` 公共 API。
- 新增 TB 侧 `xif_xaction_cfg` 作为验证环境全局 traffic 配置。
- 改造多通道 sequence：每个 ID 一个发送 task；另一个 task 按 `xaction` 配置用时钟周期随机 `lock()` / `unlock()` sequencer。
- lock 语义按你的选择实现为“阻塞其他 sequence”，不暂停本 multi-channel sequence 自己的 per-ID 发送 task。

## Key Changes
- 新增 `xif_xaction_cfg`，建议字段：
  - `enable_seq_lock`
  - `lock_gap_min_cycles / lock_gap_max_cycles`
  - `lock_hold_min_cycles / lock_hold_max_cycles`
  - `lock_iterations`
  - `id_issue_gap_min_cycles / id_issue_gap_max_cycles`
  - `beats_per_id`
- `xif_multi_channel_outstanding_seq` 增加：
  - `xaction` handle，由 test/env 设置。
  - `vif` handle，用于按 `@(posedge vif.clk)` 等待随机周期。
  - `send_id_traffic(int unsigned id)`：该 task 负责该 ID 的所有 channel/beat 发包，payload 继续沿用 `{channel[1:0], id[5:0]}` 编码到 `opcode`。
  - `random_lock_control()`：按 `xaction` 随机等待 gap 周期，调用 `lock(m_sequencer)`，再随机 hold 周期后 `unlock(m_sequencer)`。
  - `send_sem`：多个 ID task 并发时，用 semaphore 串行化同一个 sequence object 上的 `start_item/finish_item` 调用，避免多线程同时访问 UVM sequence item path。
- lock controller 不会门控本 sequence 的 ID task；它只让当前 sequence 持有 sequencer lock，从而阻塞 sibling/competing sequences。

## Test Plan
- 新增单点测试 `xif_multi_channel_lock_control_test`：
  - 配置 `xaction.enable_seq_lock=1`，gap/hold 使用确定性 min=max，便于复现。
  - 启动 multi-channel sequence，同时启动一个 competing/background sequence。
  - 预期：lock 持有期间 competing sequence 不能获得 sequencer grant；multi-channel ID task 仍能持续发包。
  - 监控 FIFO 检查：前几个观察到的 payload 来自 multi-channel IDs，background payload 只能在 unlock 后出现。
- 保留并回归现有：
  - `xif_multi_channel_outstanding_test`
  - `xif_complex_integrated_test`
  - `xif_master_slave_dual_test`
  - `xif_driver_mailbox_prefetch_test`
- 扩展 `scripts/check_waves.py`：
  - 加入新 test 的 FSDB 固定点检查。
  - 检查 lock 窗口内 multi-channel payload 顺序出现，background payload 晚于 unlock 窗口。
- 执行：
  - `make run TEST=xif_multi_channel_lock_control_test`
  - `make sim && make smoke && make complex`
  - `python3 scripts/check_waves.py --test all --xwave /home/yian/xwave/xwave`
  - 日志扫描非零 `UVM_ERROR/UVM_FATAL`。

## Skill Update
- 更新 `/home/yian/.codex/skills/xif-agent/SKILL.md`：
  - 增加“multi-channel sequence control”范式。
  - 明确推荐：复杂并发激励由 sequence 管理，agent/driver 保持 payload-agnostic。
  - 说明 sequencer lock 只用于仲裁 sibling sequences，不表示 response credit 或 agent 内部 outstanding 回收。

## Assumptions
- `xaction` 是 TB/验证环境全局 traffic 配置，不放入 `xif_cfg`。
- lock/unlock 随机间隔单位是时钟周期，因此 sequence 需要拿到 `vif`。
- ID task 的 `id` 参数对应当前 payload 编码里的 `opcode[5:0]`；channel 仍由 `opcode[7:6]` 表示。
- 本次只做 request issue 侧多通道控制，不引入 RM/SCB/response correctness compare。
