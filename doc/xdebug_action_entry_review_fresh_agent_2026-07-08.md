# xdebug 70 个 action 无上下文独立评审

生成时间：2026-07-08。评审方式：fresh agent 无上下文评审，只基于本目录列出的 evidence 文件；未读取主报告结论，未读取仓库代码，未修改 repo 文件。

## 证据范围

- 静态/catalog/schema 摘要：`action_metadata.jsonl`、`static_review.jsonl`、`static_summary.md`
- native 入口证据：`native_evidence.jsonl`、`native_summary.md`、`native_merged_best.jsonl`、`native_retry_evidence.jsonl`
- MCP 入口证据：`mcp_main_batch.ndjson`、`mcp_main_batch_results.ndjson`、`mcp_retry2_batch_meta.jsonl`、`mcp_retry2_results.ndjson`、`mcp_retry3_batch_meta.jsonl`、`mcp_retry3_results.ndjson`、`mcp_retry4_list_export_target_attempt4.xout`

## 总体结论

70 个 action 的 request schema 在“能否拒绝非法字段”上比较强：70/70 request root 和 action `args` 都是 `additionalProperties:false`。但从没有上下文的 AI 使用角度看，当前合同仍然容易迷惑，主要问题不是“完全没有 schema”，而是入口层级、状态边界、格式枚举、action 选择边界和 response 可验证性没有统一表达。

最重要的风险有四类：

1. MCP 与 native 的 envelope 不一致，且 MCP `raw_request` 在默认 XOUT 输出下会把实际成功的 native action 包装成 `XVERIF_BAD_JSON_RESPONSE`。
2. response schema 整体偏开放：静态证据显示 70/70 response 没有显式 debug 字段路径，top/summary/data 多数只能校验外壳，难以约束 debug 事实字段。
3. stateful action 容易受 session/list/config 既有状态影响，MCP 主批次出现 `list.create`、`apb.config.load`、`axi.config.load` 的 `CREATE_FAILED`，而隔离 retry 又能成功。
4. 多个 action 语义重叠，fresh agent 很容易把“查询/统计/证明/导出/协议专用/通用 stream”混用，尤其是 event/window/signal、AXI/APB vs stream、value sampling、design trace root cause 四组。

## 可交叉引用 Findings

### FRESH-RISK-01: `list.export` 的格式合同存在 schema/backend/输出三方错位

风险级别：高。

证据：

- native retry2 使用 `format: tsv` 失败，错误为 `EXPORT_FAILED`，提示 format 必须是 `u64bin` 或 `hex_tsv`。
- MCP retry2 同样使用 `format: tsv` 失败，错误相同。
- MCP retry3 改用 `format: hex_tsv`，但 schema 拒绝，错误为 `INVALID_REQUEST`，并列出 allowed values，其中包含 `tsv`、`csv`、`u64bin`，不包含 `hex_tsv`。
- native retry3 使用 `format: u64bin` 成功，但 summary 返回 `format: u64bin.v1`。
- MCP retry4 的 `list.export` XOUT 也显示 `format: u64bin.v1`，并生成 manifest 与 per-signal u64bin 文件。

评审结论：这是最容易误导 AI 的合同断裂点。schema 允许的 `tsv` 会被 backend 拒绝；backend 提示的 `hex_tsv` 又会被 schema 拒绝；成功请求使用 `u64bin`，但 response 呈现为版本化 `u64bin.v1`。主报告应把它作为 schema/catalog/backend drift 的一类代表问题，而不是单纯测试用例写错。

### FRESH-RISK-02: `axi.request_response_pair` 的 native evidence 内部不一致，MCP retry 能成功但 native retry 多次失败

风险级别：高。

证据：

- `native_summary.md` 声称 actions with ok && response schema 为 70/70，failed/blocker 为 none。
- `native_merged_best.jsonl` 只有 69 行，缺失 `axi.request_response_pair`。
- `native_retry_evidence.jsonl` 中 `axi.request_response_pair` 先因额外 `direction` 参数被 schema 拒绝，后续 retry3 与 isolate 阶段又出现 UDS/direct transport failed。
- MCP retry2 的 `axi.request_response_pair` target 也出现 direct session transport failed；MCP retry3 以更窄请求成功返回 XOUT。

评审结论：该 action 的 action 本体可能可用，但 evidence 合并口径、请求模板和运行稳定性没有对齐。对 fresh agent 来说，它会同时看到“summary 全绿”“merged best 缺失”“retry 文件失败”“MCP 最后成功”四种信号，容易做出错误结论。

### FRESH-RISK-03: MCP `raw_request` 默认输出路径会把 XOUT 误报为 JSON 错误

风险级别：高。

证据：

- MCP 主批次中 `xverif_debug_raw_request` 对 `actions`、`schema`、`batch`、`expr.normalize`、`source.context`、`session.list`、`session.doctor`、`session.gc`、`session.open`、`session.kill`、`session.close` 等入口返回 `XVERIF_BAD_JSON_RESPONSE`。
- 这些错误的 `stdout_tail` 明显包含合法的 `@xdebug.*.v1` XOUT 内容；例如 `schema`、`source.context`、`session.doctor` 都有可读 summary/data。
- native 侧同类动作在 JSON evidence 中可通过 schema 校验。

评审结论：这不是 action 语义失败，而是 MCP raw_request 与 native 默认输出格式之间的入口合同问题。fresh agent 如果只看 `ok:false` 会误判大量 action 失败；如果只看 `stdout_tail` 又会绕过 schema 验证。需要在 MCP 文档和 wrapper 行为中强制区分“需要 JSON 校验”和“需要 XOUT 可读文本”。

### FRESH-RISK-04: response schema 对 debug 事实字段约束太弱

风险级别：高。

证据：

- 静态摘要显示 response top/summary/data 开放 action 为 70/70。
- 静态解析显示 response 未显式 debug/include_debug 字段路径为 70/70。
- `stream.config.list`、`stream.config.load`、`stream.export`、`stream.query`、`stream.show`、`stream.validate` 的 summary schema 甚至是 missing 状态。
- 多数 response schema 的 `data_fields` / `summary_fields` 为空或只约束外壳，不能稳定表达关键 debug 事实。

评审结论：response 对人类可读性有一定价值，但对 AI 复用、自动比较、自动写报告和 schema 回归的价值不足。建议把每个 action 的关键事实字段固化到 summary/data schema，例如 sample count、time semantics、truncated、matched transaction、root cause path、manifest path、error recovery hint 等。

### FRESH-RISK-05: stateful registry 边界会让 MCP 主批次产生非确定失败

风险级别：中高。

证据：

- MCP 主批次中 `list.create` 对已有 list 名称返回 `CREATE_FAILED`。
- MCP 主批次中 `apb.config.load` 与 `axi.config.load` 返回 `CREATE_FAILED`，提示保存配置失败。
- native 和 MCP retry 在隔离会话或换名后，APB/AXI query、config list/load、list.delete 等动作可以成功。
- 静态 overlap 组 `stateful_registry_boundary` 覆盖 27 个 action，包含 session/list/cursor/config 多个名字空间。

评审结论：这些 action 不是纯函数。AI 如果不知道 name/session/list/config/cursor 的生命周期，会把“已有状态冲突”误判为 action 不可用。需要统一说明 name 是否幂等、是否覆盖、是否需要 delete/replace、是否 session-local。

### FRESH-RISK-06: `direction` 参数在 AXI action 之间不一致，容易从 query/cursor 迁移到分析类 action

风险级别：中高。

证据：

- `axi.query`、`axi.cursor` 允许或使用 `direction`。
- native retry2 对 `axi.latency_outlier`、`axi.outstanding_timeline`、`axi.request_response_pair` 传入 `direction` 后被 schema 拒绝，错误为 additional property。
- 后续改用 `time_range`、`limit` 等字段后，`axi.latency_outlier` 和 `axi.outstanding_timeline` 在 isolate 阶段可成功，`axi.request_response_pair` 在 MCP retry3 可成功。

评审结论：AXI action 命名都在同一 protocol group 下，fresh agent 很自然会把 `direction` 从 `axi.query` 迁移到其它 AXI 分析 action。需要在 catalog/use_for/do_not_use_for 或 schema description 中明确“哪些 AXI action 不接受 direction”。

### FRESH-RISK-07: `output` / `format` 名字在 wrapper、native envelope、action args 中重名

风险级别：中。

证据：

- MCP tool 有外层 `output_format`。
- native request 有 top-level `output`。
- `list.export` 等 action 在 inner `args` 中也有 `format` 和 `output`。
- `xverif_debug_query` 的 MCP request 又有外层 `args`，里面再嵌套 action inner `args`。

评审结论：这是 fresh agent 极易放错层级的一组字段。错误会表现为格式不对、无法 schema 校验、或输出被 wrapper 当作 XOUT/JSON 错误处理。建议主报告单列 native/MCP/SDK-free 三层 envelope 对照表。

### FRESH-RISK-08: 高复杂度 action 缺少足够强的最小模板和反例

风险级别：中。

静态复杂度最高的一组 action：

| action | complexity | required | optional | combiner | overlap |
| --- | ---: | --- | ---: | ---: | --- |
| `event.export` | 24 | `expr` | 13 | anyOf+oneOf | condition/window/event |
| `event.find` | 24 | `expr` | 13 | anyOf+oneOf | condition/window/event |
| `handshake.inspect` | 21 | `clock`,`valid`,`ready` | 7 | two oneOf | valid-ready stream |
| `expr.eval_at` | 18 | `expr`,`time`,`signals`,`clock` | 5 | oneOf | condition/value |
| `counter.statistics` | 17 | `clock`,`time_range`,`vld`,`cnt` | 4 | oneOf | signal time series |
| `signal.statistics` | 17 | `signal` | 9 | oneOf | signal time series |
| `stream.query` | 17 | `stream`,`query` | 7 | oneOf | stream/protocol |
| `value.batch_at` | 16 | `signals`,`time`,`clock` | 5 | oneOf | value sampling |

评审结论：这些 action 不一定实现差，但对无上下文 AI 来说需要“最小可用请求、常见错误请求、与邻近 action 的选择边界”。否则 AI 会从字段名相似的 action 拼装请求。

## MCP 与 native 入口差异

| 维度 | native evidence | MCP evidence | 迷惑点 |
| --- | --- | --- | --- |
| session 绑定 | JSON request 多用 `target.session_id` | `xverif_debug_query(session_id, action, args, ...)` 用外层 `session_id` | 同一概念在不同层级，AI 容易放进 inner `args` 或 native `target` |
| action 参数 | `args` 是 native action args | MCP tool 外层也叫 `args`，里面再有 action inner `args` | batch NDJSON 中 `args.args` 形态可读性差 |
| 输出格式 | native 可通过 top-level `output` 请求 JSON/compact | MCP `output_format` 与 raw_request 行为独立 | 未显式 JSON 时，raw_request 可能拿到 XOUT 并报 JSON parse 错 |
| 错误外壳 | native response 是 xdebug action response | MCP response 还有 tool-level `ok/error/response` 包装 | `ok:false` 可能是 wrapper 解析失败，不是 action 失败 |
| 状态生命周期 | native retry 多使用隔离/换名/单 action 证据 | MCP 主批次复用 session，状态残留影响 list/config | 同一 action 在主批次失败、retry 成功 |
| 可读性 | JSON evidence 适合 schema 校验 | XOUT 适合人读，但难自动比较 | AI 写报告时会在“可读”和“可验证”之间摇摆 |

## response debug 价值评审

### 有明显 debug 价值的 response 形态

- `detect_abnormal`：summary 有 finding_count 和 truncated，适合快速判断异常数量。
- `trace.active_driver` / `trace.active_driver_chain`：summary 有 requested/active time、path/hop count、termination，能支撑 root-cause 叙述。
- `source.context`：XOUT 中给出命中行和上下文，对人工 debug 价值高。
- `stream.query` / `stream.export`：summary 有 clock_edges、control/data XZ count，适合协议健康检查。
- `apb.query` / `apb.cursor`：response 包含 transaction time、addr、data、is_write、has_error，debug 价值直接。
- `list.export`：成功时给出 output_dir、manifest_file、signal index、row_count、width、columns，适合后处理。
- `scope.list`：summary 有 returned/total signal count 和 truncated，适合发现 scope 是否选错。

### debug 价值不足或机器可用性不足的 response 形态

- response schema 普遍不固定 key，实际 payload 即使有价值，也无法通过 schema 稳定约束。
- `axi.latency_outlier` 在 native isolate 成功时 summary 为空，fresh agent 难判断“没有 outlier”“没有扫描到数据”还是“只返回空壳”。
- `actions`/`schema`/`batch` 是发现和自省入口，但 MCP raw_request 默认 XOUT parse 失败会切断 AI 的自助纠错路径。
- `session.*` 在 XOUT 中包含大量状态信息，但 wrapper 报 `XVERIF_BAD_JSON_RESPONSE` 后，结构化 response 不可用；`session.close` 在 kill 后返回 session not found，缺少“already killed/closed is acceptable?” 的幂等说明。
- `stream.*` response schema 的 summary/data 约束更弱，虽然 XOUT 有信息，但 schema 无法保护字段稳定性。

## 高风险 action 清单

| action | 风险 | evidence 依据 | 建议 |
| --- | --- | --- | --- |
| `list.export` | schema/backend/response format 错位 | native retry2、MCP retry2/3/4 | 统一 allowed enum、backend accepted format、response format version |
| `axi.request_response_pair` | native evidence 缺失/失败，MCP retry 后成功 | native_summary、native_merged_best、native_retry、MCP retry2/3 | 重建单 action 稳定证据，避免 summary 70/70 与明细矛盾 |
| `actions` | 自省入口在 MCP raw_request 下误报 JSON 错 | MCP main raw_request | raw_request 默认 JSON 或错误中标注 underlying XOUT success |
| `schema` | schema 查询在 MCP raw_request 下误报 JSON 错 | MCP main raw_request | 明确 `output_format=json` 或 raw_request 不强制 JSON parse |
| `batch` | 嵌套 envelope 加剧 `args` 混淆 | static meta + MCP raw_request | 给 MCP batch 和 native batch 分别提供模板 |
| `session.list/doctor/gc/open/kill/close` | session 状态信息有价值但 MCP wrapper 误报；close/kill 幂等性不清 | MCP main raw_request | 固定 JSON response；说明 kill 后 close 的期望行为 |
| `apb.config.load` | stateful 保存失败，隔离 retry 成功 | MCP main + native retry | 明确 config replace/upsert 语义和 name 生命周期 |
| `axi.config.load` | stateful 保存失败，隔离 retry 成功 | MCP main + native retry | 同上 |
| `axi.latency_outlier` | 易误传 `direction`；成功 summary 为空 | native retry2/isolate | 给出最小模板和空结果语义 |
| `axi.outstanding_timeline` | 易误传 `direction`；窗口/采样语义复杂 | native retry2/isolate | 明确 time_range/limit/sample_point |
| `event.find` | 入口复杂、与 window/signal 重叠 | static complexity | 增加选择指南和反例 |
| `event.export` | 入口复杂、输出/inline/limit/time_range 多字段 | static + native | 增加最小模板，说明 edge/sample_time_semantics |
| `handshake.inspect` | 与 stream/query/sample pulse 重叠 | static complexity | 明确单次停滞解释 vs 通用 stream 查询 |
| `stream.query` | query/config/load/export/show/validate 组内重叠 | static + MCP | 给 stream lifecycle 图和 query schema 示例 |
| `expr.eval_at` | 与 value/window/event 都重叠 | static | 明确“表达式单点求值”边界 |
| `value.batch_at` | 与 value.at/list.value_at 重叠 | static | 明确信号列表即时值 vs 已保存 list |

## action 冲突组

### `condition_window_event`

成员：`event.export`、`event.find`、`expr.eval_at`、`signal.changes`、`signal.statistics`、`verify.conditions`、`window.verify`。

冲突点：这些 action 都能回答“某个条件什么时候成立/在窗口里是否成立/信号怎么变化”，但语义不同。fresh agent 容易用 `signal.changes` 回答 clock-sampled active cycles，或用 `event.find` 代替 `window.verify` 做全窗口证明。

建议选择边界：

- 找第一个/下一个条件事件：`event.find`
- 导出一批事件：`event.export`
- 单点表达式求值：`expr.eval_at`
- 原始跳变时间线：`signal.changes`
- clock-sampled 统计：`signal.statistics`
- 某时刻多条件检查：`verify.conditions`
- 跨窗口证明：`window.verify`

### `protocol_vs_generic_stream`

成员：APB/AXI 专用 action 与 `stream.config.*`、`stream.query`、`stream.export`、`stream.show`、`stream.validate`。

冲突点：标准总线 action 和通用 valid-ready stream action 都能查询 transaction/handshake，但 config 结构、字段命名和结果语义不同。MCP 主批次中的 APB/AXI config state failure 也说明协议专用 action 更依赖先验配置。

建议选择边界：

- 标准 APB/AXI transaction、latency、outstanding、request/response：优先 protocol action。
- 自定义 valid-ready/pipeline/FIFO/仲裁流：优先 stream action。
- 只有 `valid/ready` 两根线、想解释卡住原因：优先 `handshake.inspect`。

### `valid_ready_stream`

成员：`handshake.inspect`、`sampled_pulse.inspect`、`stream.export`、`stream.query`、`stream.show`、`stream.validate`。

冲突点：`handshake.inspect` 偏单个 valid-ready 停滞解释；`sampled_pulse.inspect` 偏 missed sampling；`stream.query/export` 偏配置化流查询。需要明确“是否已有 stream config”和“是否只是单次 handshake”。

### `signal_time_series`

成员：`counter.statistics`、`detect_abnormal`、`sampled_pulse.inspect`、`signal.changes`、`signal.stability`、`signal.statistics`。

冲突点：都处理时间序列，但有 raw transition、clock-sampled statistics、counter distribution、异常扫描、稳定性、missed pulse 六种任务。当前命名不足以让 fresh agent 自动区分。

### `value_sampling`

成员：`expr.eval_at`、`list.value_at`、`value.at`、`value.batch_at`。

冲突点：都能“取值”。边界应固定为：单信号即时值用 `value.at`；多信号即时值用 `value.batch_at`；已保存 list 用 `list.value_at`；表达式用 `expr.eval_at`。

### `design_trace_root_cause`

成员：`signal.canonicalize`、`signal.resolve`、`source.context`、`trace.active_driver`、`trace.active_driver_chain`、`trace.driver`、`trace.load`。

冲突点：静态 resolve/driver/load/source 和 waveform-aware active driver/chain 混在一起。边界应固定为：没有时间点或波形时用静态 trace；有时间点和 waveform/design combined session 时用 active driver/chain；需要源码上下文时再接 `source.context`。

### `stateful_registry_boundary`

成员覆盖 session/list/cursor/config 共 27 个 action。

冲突点：`name` 在 session、list、cursor、APB config、AXI config、event config、stream config 中重复出现，但生命周期和幂等性不同。MCP 主批次的 `CREATE_FAILED` 说明这是实际运行风险，不只是文档问题。

### `meta_dispatch`

成员：`actions`、`schema`、`batch`。

冲突点：这些 action 是 AI 自助发现入口，但在 MCP raw_request 默认 XOUT 情况下会被包装成 JSON 错误。`batch` 又有 native request 嵌套 request，和 MCP batch NDJSON 的 tool args 嵌套叠加，最容易造成 envelope 混淆。

## 优先修复建议

1. 先修 `list.export` 合同：schema enum、backend accepted format、error hint、response format version 四处必须一致。
2. 重跑并重建 `axi.request_response_pair` native 单 action evidence，保证 summary、merged_best、retry 文件不互相矛盾。
3. MCP `raw_request` 明确输出合同：要么默认 JSON，要么在 `XVERIF_BAD_JSON_RESPONSE` 中明确标注 underlying xdebug returned XOUT，并提供如何重试为 JSON。
4. 为所有 response schema 固化关键 debug 字段；至少给每个 action 的 summary/data 定义稳定字段集合。
5. 写三层入口对照表：native JSON、FastMCP `xverif_debug_query`、MCP batch NDJSON，重点标出 `session_id`、outer `args`、inner `args`、`output_format`、native `output`、action `args.output`。
6. 给高复杂度 action 增加最小模板和反例，优先 `event.find/export`、`handshake.inspect`、`expr.eval_at`、`signal.statistics`、`counter.statistics`、`stream.query`、`value.batch_at`。
7. 对 stateful registry action 增加 lifecycle 说明：create/load 是否幂等、name 冲突如何处理、replace/upsert/delete 语义、session-local 范围。
8. 在 catalog 中加入 action selection matrix，覆盖本报告列出的 8 个冲突组。

## 主报告交叉引用建议

- 引用 `FRESH-RISK-01` 说明 schema/backend drift 的具体证据。
- 引用 `FRESH-RISK-02` 说明 evidence aggregation 与 retry 口径不一致。
- 引用 `FRESH-RISK-03` 说明 MCP/native 默认输出格式差异。
- 引用 `FRESH-RISK-04` 说明 response schema 对 debug 事实字段缺少约束。
- 引用 `FRESH-RISK-05` 说明 stateful action 的 name/session/config/list 生命周期问题。
- 引用 action 冲突组章节作为 action selection matrix 的初稿输入。

