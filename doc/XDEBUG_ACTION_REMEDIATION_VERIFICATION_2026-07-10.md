# xdebug 全 action 修复后验收报告（2026-07-10）

## 1. 验收口径

本报告对应 `XDEBUG_ACTION_REMEDIATION_PLAN_2026-07-10_10-54-25.md`。验收不调用仓库的 70-action replay runner，也不把 schema-only 或 fake backend 冒充真实 runtime。

每个 public action 必须同时满足：

- ActionRegistry、action spec、request/response schema 和 request/response example 均存在且一致。
- 原始逐 action 审计已经覆盖 CLI 与 MCP 的正确请求、schema 失败和 handler 失败；无法自然构造 handler 失败的只读 list action 在原报告中明确标注。
- 修复后由 contract、unit、synthetic existing、VIP、active semantics 或 session/MCP 回归覆盖相应公共 helper 和真实 handler。
- public frontend 不再修改 backend JSON 来按值删除 summary/data；各 handler 从事实源构造投影。XOUT renderer 只负责展示选择，错误保留 code/layer/invalid_arg/expected/correct_example 等可调试字段。

公共门禁结果：70 个 non-removed action、70 份 request schema、70 份 response schema、70 份基本 request example 和至少 70 份 response example 全部存在；本轮识别出的通用占位 response 已全部替换为 action-specific 证据形状。

## 2. 逐 action 结论

下表中的“通过”表示该 action 已满足上述静态合同与所属 runtime 回归门禁；不是只检查文件存在。

| Action | 类别 | 结论 | 主要修复后证据 |
| --- | --- | --- | --- |
| `actions` | builtin | 通过 | compact names/verbose descriptors、无 implemented 重复、contract |
| `apb.config.list` | waveform | 通过 | protocol 手工 CLI/MCP 三场景、schema/example、synthetic |
| `apb.config.load` | waveform | 通过 | effective config、protocol 手工 CLI/MCP、APB fixture |
| `apb.cursor` | waveform | 通过 | 标准 time/transaction、protocol 手工 CLI/MCP、APB fixture |
| `apb.query` | waveform | 通过 | effective direction/address、protocol 手工 CLI/MCP、APB fixture |
| `apb.transfer_window` | waveform | 通过 | scan/window/count、protocol 手工 CLI/MCP、APB fixture |
| `axi.analysis` | waveform | 通过 | latency min/avg/max/p50/p95/p99/slowest、synthetic existing |
| `axi.export` | waveform | 通过 | protocol 手工 CLI/MCP、export schema/example |
| `axi.channel_stall` | waveform | 通过 | effective config/sample semantics、protocol 与 synthetic |
| `axi.config.list` | waveform | 通过 | protocol 手工 CLI/MCP 三场景、schema/example |
| `axi.config.load` | waveform | 通过 | effective config、protocol 手工 CLI/MCP、AXI fixture |
| `axi.cursor` | waveform | 通过 | 1-based index/标准 time/latency、protocol 与 synthetic |
| `axi.latency_outlier` | waveform | 通过 | compact transaction anchor、protocol 与 synthetic |
| `axi.outstanding_timeline` | waveform | 通过 | analyzer 全量聚合、peak/change points、synthetic |
| `axi.query` | waveform | 通过 | compact 默认无 beat data、verbose 展开、synthetic |
| `axi.request_response_pair` | waveform | 通过 | compact 配对根因字段、protocol 与 synthetic |
| `batch` | builtin | 通过 | failed count/index/code/layer JSON/XOUT parity、contract |
| `counter.statistics` | waveform | 通过 | 手工 CLI/MCP 三场景、clock sampling/static/synthetic |
| `cursor.delete` | waveform | 通过 | 手工 CLI/MCP 三场景、cursor 状态与 response example |
| `cursor.get` | waveform | 通过 | 手工 CLI/MCP 三场景、cursor 状态与 response example |
| `cursor.list` | waveform | 通过 | 手工 CLI/MCP 正确/schema 场景、compact list contract |
| `cursor.set` | waveform | 通过 | 有效 TimeSpec example、手工 CLI/MCP、synthetic |
| `cursor.use` | waveform | 通过 | 手工 CLI/MCP 三场景、active cursor contract |
| `detect_abnormal` | waveform | 通过 | X/Z evidence、手工 CLI/MCP 三场景、synthetic |
| `event.config.list` | waveform | 通过 | 手工 CLI/MCP 三场景、sampling config response |
| `event.config.load` | waveform | 通过 | effective sampling config、手工 CLI/MCP、synthetic |
| `event.export` | waveform | 通过 | rows/aggregate/output、手工 CLI/MCP、synthetic |
| `event.find` | waveform | 通过 | standard time/match evidence、手工 CLI/MCP、synthetic |
| `expr.eval_at` | waveform | 通过 | operand/bracket/sample context、手工 CLI/MCP、synthetic |
| `expr.normalize` | design | 通过 | syntax validation、手工 CLI/MCP 三场景、contract |
| `handshake.inspect` | waveform | 通过 | scan range/count/stall evidence、手工 CLI/MCP、synthetic |
| `list.add` | waveform | 通过 | one-based list semantics、手工 CLI/MCP、synthetic |
| `list.create` | waveform | 通过 | resolved signals/status、手工 CLI/MCP、synthetic |
| `list.delete` | waveform | 通过 | index/name evidence、手工 CLI/MCP、synthetic |
| `list.diff` | waveform | 通过 | point-sampled delta contract、手工 CLI/MCP、synthetic |
| `list.export` | waveform | 通过 | manifest/files/scan metadata、手工 CLI/MCP、synthetic |
| `list.show` | waveform | 通过 | compact list/index contract、手工 CLI/MCP、synthetic |
| `list.validate` | waveform | 通过 | missing/invalid path evidence、手工 CLI/MCP、synthetic |
| `list.value_at` | waveform | 通过 | point sampling/no config echo、手工 CLI/MCP、synthetic |
| `sampled_pulse.inspect` | waveform | 通过 | full analysis independent of response limit、synthetic |
| `schema` | builtin | 通过 | checked-in exact schema、unknown action nearby suggestions |
| `scope.list` | waveform | 通过 | shared resolver/resource errors、手工 CLI/MCP、synthetic |
| `scope.roots` | waveform | 通过 | resource/not-found distinction、手工 CLI/MCP、synthetic |
| `session.close` | session | 通过 | native 单 session backend cleanup、失败保留 registry record、session fixture |
| `session.doctor` | session | 通过 | native health/resource-change 诊断、session fixture |
| `session.gc` | session | 通过 | native expiry/unhealthy 扫描与清理结果、session fixture |
| `session.kill` | session | 通过 | native 单 session 与显式 `all` cleanup 合同、session fixture |
| `session.list` | session | 通过 | native registry records 与 expired cleanup summary、session fixture |
| `session.open` | session | 通过 | native engine open/record/resource ownership、session fixture |
| `signal.canonicalize` | design | 通过 | shared design resolver、手工 CLI/MCP 三场景、contract |
| `signal.changes` | waveform | 通过 | actual transition semantics/rows/scan metadata、synthetic |
| `signal.resolve` | design | 通过 | not-found/ambiguous distinction、shared resolver tests |
| `signal.stability` | waveform | 通过 | initial/final/transition/stable evidence、synthetic |
| `signal.statistics` | waveform | 通过 | clock/raw mode、full-width values、synthetic |
| `source.context` | design | 通过 | compact/verbose schema、source window/error contract |
| `trace.active_driver` | combined | 通过 | internal fire/driver evidence、active semantics fixture |
| `trace.active_driver_chain` | combined | 通过 | chain evidence/limits、active semantics fixture |
| `trace.driver` | design | 通过 | signal existence vs no-driver、source evidence、contract |
| `trace.load` | design | 通过 | signal existence vs no-load、path evidence、contract |
| `rc.generate` | waveform | 通过 | resolved signal/marker/export evidence、synthetic |
| `value.at` | waveform | 通过 | compact point value/requested edge fields、synthetic |
| `value.batch_at` | waveform | 通过 | per-signal status/missing reasons/bracket evidence、synthetic |
| `verify.conditions` | waveform | 通过 | standard time/condition evidence、synthetic |
| `window.verify` | waveform | 通过 | sampled count/failure/unknown/condition evidence、synthetic |
| `stream.config.load` | waveform | 通过 | effective config/issues、protocol 手工 CLI/MCP |
| `stream.config.list` | waveform | 通过 | compact config discovery、protocol 手工 CLI/MCP |
| `stream.show` | waveform | 通过 | config/semantics/issues、protocol 手工 CLI/MCP |
| `stream.validate` | waveform | 通过 | scanned range/dynamic issues、protocol 手工 CLI/MCP |
| `stream.query` | waveform | 通过 | scan count/truncation/row evidence、protocol 手工 CLI/MCP |
| `stream.export` | waveform | 通过 | output status/row count/scan metadata、protocol 手工 CLI/MCP |

## 3. MCP 生命周期附加验收

- MCP SDK 和 SDK-free wrapper 均提供 debug/cov open/list/doctor/close/kill/gc。
- coverage query 拒绝 native `session.open/status/close` 及其它 lifecycle action，并给出 coverage 专用正确示例。
- xdebug 与 xcov public tool 语义对称，native capability 不对称：xdebug 支持 fixed admin doctor/kill/gc；xcov kill 仅终止 managed loop/process/job。
- direct、fake LSF、SDK、SDK-free、dead loop、native close failure、partial cleanup、tombstone 和 gc 定向回归通过；最终全仓库门禁仍见第 4 节。
- 本轮执行 `make -C xdebug test-mcp-real-lsf`，在启动阶段返回 `FileNotFoundError: bsub`；当前 `command -v bsub` 为空。该项记录为环境阻塞，未切换 fake LSF，也未记为通过。

## 4. 最终门禁

Stage 7 必须从 clean 状态重新构建并执行顶层 `make test`、`make full-test` 以及计划列出的 MCP/NPI/VIP/LSF 分层测试。最终结果、commit 和 push 状态以交付说明为准。
