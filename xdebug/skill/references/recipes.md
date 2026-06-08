# xdebug 常见 Debug Recipes

本文是面向 AI agent 的工作流速查。执行时仍以 runtime `actions`、`schema` 和 examples 为最终契约。

## 通用原则

- 先 compact 查询，再按需 include。
- 先定位时间点，再做因果 join。
- 同一时间多信号取值用 `value.batch_at`。
- 不要导出全量 rows/samples/transactions 作为第一步。
- 最终结论必须保留 `signal/time/value/file:line/finding/confidence/truncated`。

## Ready 卡低

1. `value.batch_at` 取 `valid/ready/full/state/backpressure`。
2. `signal.changes` 查 `ready` 首次卡住或最后变化时间。
3. 有 `daidir + fsdb + time` 时用 `trace.active_driver` 查当前生效 driver。
4. 若 driver 指向 `full`、state 或 grant 条件，补 `value.at` / `value.batch_at` 查控制信号值。
5. 用 `source.context include_source:true` 获取最终源码 evidence。

请求骨架：

```json
{
  "api_version": "xdebug.v1",
  "action": "value.batch_at",
  "target": {"session_id": "case_a"},
  "args": {
    "time": "@stall",
    "signals": ["top.u.valid", "top.u.ready", "top.u.full", "top.u.state_q"],
    "format": "hex"
  }
}
```

## Valid 有脉冲但没被接受

1. `event.find` 或 `event.export` 查 `valid && !ready`。
2. `handshake.inspect` 找 long stall 或 accept gap。
3. `value.batch_at` 取 payload、ready、backpressure、state。
4. `trace.driver` 或 `trace.active_driver` 查 ready/backpressure 的设计原因。

表达式查找：

```json
{
  "api_version": "xdebug.v1",
  "action": "event.find",
  "target": {"session_id": "case_a"},
  "args": {
    "expr": "valid && !ready",
    "clk": "top.clk",
    "signals": {
      "valid": "top.u.valid",
      "ready": "top.u.ready"
    },
    "time_range": {"begin": "0ns", "end": "100us"},
    "mode": "first"
  }
}
```

## AXI latency 异常

1. `axi.analysis` 用 compact 查 latency/outstanding/response findings。
2. 对 top abnormal 的 begin/end 设 cursor 或直接用 TimeSpec。
3. `value.batch_at` 查 AW/W/B/AR/R channel 的 valid/ready/id/resp。
4. 只有需要证明异常 transaction/beat 时才加 `include_transactions:true` 或 `include_beats:true`。

```json
{
  "api_version": "xdebug.v1",
  "action": "axi.analysis",
  "target": {"session_id": "wave_case"},
  "args": {
    "name": "axi0",
    "analysis": "latency",
    "direction": "read",
    "time_range": {"begin": "0ns", "end": "100us"},
    "max_items": 20
  }
}
```

## APB slow/error access

1. `apb.query` 查 error/slow findings。
2. 对 finding 的 time/window 用 `value.batch_at` 取 `psel/penable/pready/pslverr/paddr/pwrite`。
3. 只有需要完整访问明细时才加 `include_accesses:true`。

## X/Z 传播

1. `detect_anomaly` 查 `unknown_xz`。
2. `signal.changes` 找第一个 X/Z 出现时间。
3. 有 combined 资源时用 `trace.active_driver` 在该时间查生效 driver。
4. 用 `trace.graph` 向上游扩展，寻找 X/Z 源。

## Counter / FSM 异常

1. `value.at` 或 `signal.trend` 确认状态/计数异常。
2. `fsm.explain` 或 `counter.explain` 获取更新规则。
3. `trace.driver` 查更新条件。
4. `source.context` 获取 `file:line` evidence。

## 当前生效 driver join

使用条件：必须有 `daidir + fsdb + signal + requested_time`。

1. 波形侧先定位异常时间。
2. `value.batch_at` 取相关信号。
3. `trace.active_driver` 做 active trace。
4. `resolved`：保留 driver kind、file/line、active_time、control evidence。
5. `control_only`：继续查 control signal 值，不要宣称已解析 direct data driver。
6. `unresolved`：回到 `trace.driver`、`trace.graph` 或源码 `rg` 缩小信号路径。

```json
{
  "api_version": "xdebug.v1",
  "action": "trace.active_driver",
  "target": {"session_id": "case_a"},
  "args": {
    "signal": "top.u.ready",
    "requested_time": "@stall",
    "include_control": true,
    "include_parity": true
  }
}
```

## 生成波形证据视图

当用户需要给 nWave 打开同一组信号、marker、analog 波形或表达式信号时，用 `rc.generate`。不要让 AI 手写 rc；由 xdebug 校验 FSDB 信号和 marker 时间后生成。

详细配置见 [rc-generate.md](rc-generate.md)。
