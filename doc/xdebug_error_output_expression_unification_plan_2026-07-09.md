# xdebug 错误反馈、输出合同与表达式统一修复计划（2026-07-09）

## 背景

本计划基于 `doc/xdebug_mcp_action_return_common_issues_2026-07-09.md` 和后续 grill-with-docs 计划对齐讨论。现有问题不是单个 action 文档或示例漂移，而是三层合同不统一：

- MCP wrapper 层：错误路径和成功路径格式不一致，`correct_example` 容易混入 native envelope，raw request 失败会把 backend 结构化错误包进 `stdout_tail`。
- schema/validator 层：required、additionalProperties、conditional required、`did_you_mean` 和示例生成策略不稳定。
- action handler 层：语义错误经常退化为 `ACTION_FAILED + message`，缺少 `invalid_arg`、`allowed_values`、`correct_example` 和 `next_actions`，且存在坏值回显、字段路径不准、空结果语义不一致等问题。

本轮目标是先建立统一公共合同，再按阶段批量迁移 action。API 行为不提供兼容迁移期；实施过程分阶段回归、分阶段 commit。

## 已确认决策

### 1. 修复策略

采用“先建统一错误反馈合同，再批量迁移 action”的策略。不按 57 条问题逐条零散修补。

### 2. MCP xout 错误路径

MCP `output_format:"xout"` 下，backend/action 返回 `ok:false` 时也应渲染为 xout error，同时保留结构化错误字段。tool/transport 级失败仍作为 MCP/tool 层结构化错误。

### 3. correct_example 当前入口唯一

`correct_example` 永远只表示当前入口的正确形态：

- native CLI / `tools/xdebug --json -`：backend 只产 native JSON envelope 示例。
- MCP `xverif_debug_query`：MCP wrapper 将 native 示例翻译成 MCP tool 参数形态。
- MCP `xverif_debug_raw_request`：属于 native envelope 入口，显示 native 示例。

同一个错误中不同时出现 native 和 MCP 两套 example。

示例说明与示例 JSON 分开：

- `example_note` 是 string，用于说明这是示例、是否包含占位符、下一步应如何替换。
- `correct_example` 只放当前入口请求形态，不嵌入 `executable`、`note`、`kind` 等元字段。

### 4. 统一 ErrorBuilder，不兼容旧弱错误

新增公共 `ErrorBuilder / DiagnosticError`，由 handler 显式构造结构化错误。所有 action handler 的参数语义错误、资源不存在、配置不存在、signal not found、enum-like 检查都必须走统一 builder。

不保留旧弱错误兼容：

- 不再允许迁移后的 action 返回 `{"error":"ACTION_FAILED","message":"..."}` 这类弱形态。
- 错误诊断字段统一放在 `error` 下；`data` 不承载错误提示。
- 不继续把旧 `data.correct_example` 当兼容字段。

### 5. error_layer 必填

所有 `ok:false` 错误必须带 `error_layer`，由产生错误的代码层填写，不由用户传入。

枚举：

- `schema`：JSON schema / action request schema 校验失败。
- `handler`：action handler 语义校验或领域资源错误。
- `wrapper`：MCP wrapper 或 CLI wrapper 参数壳问题。
- `transport`：session transport、UDS/TCP/file exchange、engine process、timeout。
- `internal`：未预期异常或不可恢复内部 bug。

### 6. 禁止泛化错误 code 承载明确错误

新代码不得使用泛化 `ACTION_FAILED`、`INVALID_REQUEST`、`ADD_FAILED` 承载明确参数、资源、状态错误。高频旧泛化 code 分批迁移。

明确错误优先使用稳定 code，例如：

- `MISSING_FIELD`
- `INVALID_ARGUMENT`
- `INVALID_ENUM`
- `INVALID_TIME`
- `SIGNAL_NOT_FOUND`
- `CLOCK_NOT_FOUND`
- `CONFIG_NOT_FOUND`
- `LIST_NOT_FOUND`
- `FILE_NOT_FOUND`
- `RESOURCE_NOT_READABLE`
- `SESSION_NOT_FOUND`
- `PRECONDITION_FAILED`

`ACTION_FAILED` 只允许表示非参数类 action 执行失败，且必须附更具体 `cause_code`。

### 7. not-found / empty / partial 统一语义

输入对象不存在时返回 error：

- 例如 signal path、config name、list name、file/config_path 不存在。
- `ok:false`，使用具体 not found code。
- 必须包含 `invalid_arg`、`expected`、`next_actions`、`correct_example`。

输入对象存在但查询结果为空时返回成功空结果：

- `ok:true`
- `summary.status:"empty"`
- `summary.empty_reason:"no_match | no_children | no_change | filtered_out | no_active_evidence"` 等。

批量 action 部分失败时返回 partial：

- `ok:true`
- `summary.status:"partial"`
- 包含 `requested_count`、`ok_count`、`missing_count`、`partial_failure:true` 等计数。
- 每条 row 保留自身状态。

### 8. xout 信息分层

compact xout 要统一第一屏 summary，表格 section 只放 rows，metadata 不混入表格块尾部。

信息分层：

- `summary`：第一屏结论和关键计数。
- `data`：核心 domain evidence，例如 time/value/signal/path/cause/transaction/event。
- `warnings/findings`：异常、partial、可疑配置。
- `args.output.verbose:true` 或 full JSON：绝对路径、pid、inode、mtime、socket、transport 原始字段等底层排障信息。

必要 debug 信息不能丢。超长字段默认截断并标 `truncated:true`，可通过 verbose 或 export 获取更多内容。

### 9. 输出详细度和 output 对象

全体系只允许一个 verbose：

- 允许：`args.output.verbose`
- 禁止：MCP tool 顶层 `verbose`、native envelope 顶层 `output.verbose`、`args.verbose`、`include_*`、其它 action-specific verbose 别名。

删除所有 public `include_*` 参数，统一由 `args.output.verbose` 控制详细输出。

不引入 `output.max_bits`。超长 bit、payload 或 evidence 的展示通过 action 默认截断、`truncated:true`、`args.output.verbose` 和必要时 export 处理，不新增公共 bit 宽度参数。

MCP/tool 层 `output_format` 和 action 导出文件格式彻底区分：

- MCP/tool 顶层 `output_format`：工具返回格式，取值 `xout | json`。
- xdebug action 层 `args.output.file_format`：导出文件内容格式，取值按 action schema 限定。

禁止 `args.output.format`，错误提示应给 `did_you_mean:"args.output.file_format"`。

文档和 skill 示例应避免无解释地同时出现 `output_format` 与 `args.output.file_format`。只有专门解释二者区别的段落才允许并列展示。

统一 `output` 对象：

```json
{
  "output": {
    "path": "out/events.tsv",
    "file_format": "tsv",
    "verbose": false
  }
}
```

禁止旧输出字段：

- `output_file`
- `output_dir`
- `output_prefix`
- `rc_path`
- 顶层 `args.path` 作为输出路径

### 10. line_limit 替代 limit

不再使用单独 `limit`，统一改为 `line_limit`，避免与扫描限制混淆。

- `args.line_limit`：控制 response/xout 中 item、finding、event、transaction、row 的最大返回数量。
- 所有旧参数拒绝：`limit`、`max_items`、`max_events`、`max_samples`、`max_findings`、`max_examples`。
- 错误提示：`invalid_arg:"args.limit"`、`did_you_mean:"args.line_limit"`。
- 扫描深度使用明确字段，例如 `scan_limit`、`sample_limit`、`cycle_limit`。
- response summary 包含 `returned_count`、`line_limit`、`truncated`，如可得再给 `total_count` 或 `estimated_total_count`。

### 11. MCP 禁止 native session action

MCP 入口内所有 session 生命周期只能通过 MCP session tools。

`xverif_debug_query` 和 `xverif_debug_raw_request` 均禁止 native `session.*` action：

- `session.open`
- `session.close`
- `session.kill`
- `session.gc`
- `session.doctor`
- `session.list`

返回 `error_layer:"wrapper"`，`correct_example` 给 MCP session tool 形态，例如 `xverif_debug_session_open`、`xverif_debug_session_close`、`xverif_debug_session_list`。

不提供 `allow_native_session_action`、opt-in flag 或 raw_request 绕行入口。MCP 入口内所有 session 生命周期必须使用 MCP session tools。

### 12. config.list 统一

所有 `*.config.list` 统一：

- `args:{}`：列出全部 loaded config。
- `args.name` 或协议对应 key：显示单个 config 详情。
- name 不存在时返回结构化 `CONFIG_NOT_FOUND`，包含 `invalid_arg:"args.name"`、`available_values`、`next_actions`、`correct_example`。

### 13. export 统一

所有 `*.export` 统一支持可选 `args.output.path`：

- 传 `output.path`：写文件或目录。
- 不传 `output.path`：返回 response preview。

response summary 标明：

- `status`
- `output_written`
- `output.path` 如适用
- `row_count`
- `truncated`

### 14. 时间合同统一

Request：

- 单点时间只用 `args.time`。
- 时间窗口只用 `args.time_range.begin` / `args.time_range.end`。
- 不接受 `at`、`requested_time`、`start_time`、`begin/end`、`from/to` 作为请求字段。

Response / xout：

- 用户可见时间字段必须带单位字符串，例如 `"255ns"`。
- 原始 tick 字段必须命名为 `*_ticks`。
- 旧字段错误必须给 `invalid_arg`、`did_you_mean`、`correct_example`。

### 15. query 形态统一

统一 `*.query` 参数形态与 enum 可发现性，不合并 action 能力。

- `*.query` 的 query kind 必须可枚举。
- object query 必须有 `kind` 或 `type`，并完整约束 required 和 `additionalProperties:false`。
- 同协议族同名字段 enum 尽量统一，例如 `direction: read | write | all`。
- 不支持的枚举值必须给 `allowed_values`、`did_you_mean`、`suggestion`。

### 16. 表达式组件一次性统一

一次性迁移所有表达式相关 action，不做逐步双轨。

统一表达式组件能力基线采用当前 stream `StreamExpression`：

- signal / literal
- bit select
- concat
- unary `!` / `~`
- logical `&&` / `||`
- bitwise `&` / `|` / `^`
- compare `==` / `!=` / `>` / `>=` / `<` / `<=`

一次性迁移：

- `stream.*` 表达式解析与求值
- `event.find`
- `event.export`
- `expr.eval_at`
- `window.verify`
- `verify.conditions`
- `handshake.inspect` 中表达式相关入口

统一错误：

- unknown alias
- missing signal value
- parse error
- select out of range
- unsupported operator
- X/Z unknown

### 17. 表达式 alias 规则

通用 action 表达式只允许 alias，不允许直接 signal path。真实 signal path 必须通过 `signals` map 或 config 绑定。

正确形态：

```json
{
  "signals": {
    "valid": "top.u.valid",
    "ready": "top.u.ready"
  },
  "expr": "valid && ready"
}
```

如果用户直接在 expr 写真实路径，返回错误：

- `invalid_arg:"args.expr"`
- `expected:"use alias in expr and put real signal path in args.signals"`
- `correct_example`

### 18. verify/window condition 形态

`verify.conditions` 和 `window.verify` 迁移到 `args.signals + conditions[].expr` 形态。

示例：

```json
{
  "time": "100ns",
  "clock": "clk",
  "signals": {
    "clk": "top.u.clk",
    "valid": "top.u.valid",
    "ready": "top.u.ready",
    "cnt": "top.u.counter"
  },
  "conditions": [
    {
      "name": "fire",
      "expr": "valid && ready"
    },
    {
      "name": "counter_threshold",
      "expr": "cnt >= 8'd16"
    }
  ]
}
```

`conditions[].signal/op/value` 旧形态不兼容，直接拒绝并给新示例。

### 19. stream / event config alias 化

stream config 入口也统一 alias 化。真实 signal path 只允许出现在 `signals` map value 中，config 字段只写 alias/expression。

示例：

```json
{
  "name": "req_stream",
  "signals": {
    "clk": "top.u.clk",
    "rst_n": "top.u.rst_n",
    "req_vld": "top.u.req_vld",
    "req_rdy": "top.u.req_rdy",
    "req_data": "top.u.req_data"
  },
  "clock": "clk",
  "reset": "rst_n",
  "vld": "req_vld",
  "rdy": "req_rdy",
  "data": "req_data",
  "beat_fields": {
    "opcode": "req_data[7:0]"
  }
}
```

`clock` 必须是 alias，不允许直接真实 clock path。

event config 同样统一 alias 化。AXI/APB config 不纳入 alias 化改造。

### 20. 无兼容迁移期

API 行为直接切换到新合同，不保留旧 alias 或 runtime warning 兼容。

旧字段和旧形态直接拒绝，并通过结构化错误给新 `correct_example`：

- `limit`
- `include_*`
- `output.format`
- `from/to`
- stream 直接 path config
- `conditions[].signal/op/value`
- native session action through MCP

## 分阶段实施计划

### 阶段 1：计划与合同文档

范围：

- 写入本计划文档。
- 必要时新增 ADR / glossary，说明 error contract、expression alias contract、output contract。
- 不改源码。

验证：

- `git diff --check`
- 人工检查计划覆盖已确认决策。

commit 边界：

- 只提交文档。

### 阶段 2：公共错误合同

范围：

- 新增 `ErrorBuilder / DiagnosticError`。
- 调整 `make_error` / `ResponseBuilder` / dispatcher / engine server 错误接入。
- schema validator 错误加 `error_layer:"schema"`。
- handler 层可先迁移一小批代表 action，证明新 builder 可用。
- xout renderer 支持 `error_layer`、`example_note`、当前入口 `correct_example`、`next_actions`、`allowed_values`、`did_you_mean`。

验证：

- error schema / response schema 更新并校验。
- 单元测试覆盖 ErrorBuilder。
- contract test 覆盖 schema 层和 handler 层参数错误。

commit 边界：

- 公共错误结构和代表 action 迁移可独立提交。

### 阶段 3：MCP 入口合同

范围：

- MCP `output_format:"xout"` 下 backend `ok:false` 渲染为 xout error。
- MCP wrapper 把 native `correct_example` 翻译成当前 MCP tool 形态。
- `xverif_debug_query` 与 `xverif_debug_raw_request` 禁止 native `session.*`。
- raw_request backend error 从 `stdout_tail` 提升为结构化 backend error。

验证：

- MCP wrapper unit tests。
- MCP action negative tests。
- 检查错误路径不再混 native envelope 到 MCP example。

commit 边界：

- MCP 入口合同单独提交。

### 阶段 4：输出与参数词典

范围：

- `line_limit` 替代 `limit/max_*`。
- `args.output.path/file_format/verbose` 统一。
- 删除 public `include_*`。
- 时间字段统一。
- `*.export` 支持 `output.path`，不传则 preview。
- `*.config.list` 全部统一 list/show 语义。

验证：

- schema-test。
- contract-test。
- docs/examples schema validation。
- 重点 negative tests：旧字段全部拒绝并给 `did_you_mean`。

commit 边界：

- 参数词典与输出合同单独提交。

### 阶段 5：统一表达式组件

范围：

- 抽取统一 waveform expression evaluator，能力覆盖 stream 当前表达式语义。
- stream/event/expr/window/verify/handshake 一次性迁移。
- 表达式 action 统一 alias map。
- stream/event config alias 化；AXI/APB 不改。
- `verify.conditions` / `window.verify` 改为 `conditions[].expr`。

验证：

- expression unit tests。
- schema tests。
- handler negative tests：unknown alias、parse error、select out of range、unsupported operator。
- runtime focused probes：`== != > >= < <=`、bit select、concat、X/Z unknown。

commit 边界：

- 表达式统一单独提交。

### 阶段 6：action family 批量迁移

范围：

- 按 family 修 handler 层错误、empty/partial summary、xout 信息分层。
- 建议顺序：
  - value/list/scope/signal
  - event/expr/window/verify
  - stream/handshake
  - trace/rc/source
  - AXI/APB

验证：

- 每批跑关联 schema/contract/runtime focused probes。
- 涉及真实 FSDB/NPI/VCS/VIP 的测试按 AGENTS.md 在沙箱外执行。

commit 边界：

- 每个 family 或小批次回归后 commit。

### 阶段 7：skill/docs/mirror

范围：

- 更新 `skills/xverif-cli` 与 `skills/xverif-mcp` 全量 API 说明和示例。
- 同步安装版 skill mirror。
- 更新 `doc/agents/xdebug/` 中受影响架构说明。

验证：

- 文档示例 schema 校验。
- `git diff --check`
- skill mirror `diff -qr`。

commit 边界：

- 文档与 skill mirror 单独提交。

## 全阶段任务书

### 阶段 1 任务：冻结计划和证据

目标：把本轮破坏性 API 决策、证据来源和阶段边界固化为 repo 内文档，作为后续实现的 source of truth。

任务：

- 保留逐项评审报告 `doc/xdebug_mcp_action_return_review_2026-07-09.md`。
- 保留共性问题报告 `doc/xdebug_mcp_action_return_common_issues_2026-07-09.md`。
- 新增本计划书 `doc/xdebug_error_output_expression_unification_plan_2026-07-09.md`。
- 计划书覆盖错误合同、MCP/native 入口差异、输出合同、表达式 alias 合同、分阶段回归和 commit 边界。
- 文档阶段不修改源码、不修改 schema、不同步 skill mirror。

验收：

- `git diff --check`
- `git status --short` 只包含本阶段文档。
- 文档单独 commit。

当前状态：

- 已完成并提交：`f2c6550 文档：记录 xdebug MCP action 返回评审与统一修复计划`。

### 阶段 2 任务：公共错误合同垂直切片

目标：先把统一错误对象从 schema 层、handler 层、response builder、xout renderer 到测试跑通，证明公共抽象可用。

任务：

- 新增公共错误类型：
  - `DiagnosticError` 或等价结构。
  - `ErrorBuilder` helper。
  - 支持 `code`、`message`、`recoverable`、`error_layer`、`invalid_arg`、`expected`、`received`、`allowed_values`、`did_you_mean`、`missing_name`、`missing_resource`、`available_values`、`next_actions`、`example_note`、`correct_example`、`cause_code`。
- 更新 response/error schema：
  - 错误诊断字段统一位于 `error`。
  - `data` 不再承载错误提示。
  - `error_layer` 为必填枚举。
- 更新 response builder：
  - `make_error` 支持结构化 error object。
  - `ResponseBuilder::error` 支持 `DiagnosticError`。
  - 保持成功 response 不受影响。
- 更新 schema validator 接入：
  - schema 校验失败填 `error_layer:"schema"`。
  - schema 旧 `data.invalid_arg/correct_example` 迁移到 `error`。
  - additionalProperties、required、enum、conditional required 继续给 `invalid_arg/did_you_mean/correct_example`。
- 更新 xout renderer：
  - xout error 展示 `code/message/recoverable/error_layer`。
  - 展示 `invalid_arg/expected/received/allowed_values/did_you_mean`。
  - 先展示 `example_note`，再展示 `correct_example`。
  - 展示 `next_actions`。
- 迁移代表 action：
  - `schema`：`args.kind` 非法 enum 走 handler 层 `INVALID_ENUM`。
  - `value.at`：缺字段、非法 time、signal not found、clock not found 至少覆盖一种 handler 错误。
  - `list.add` 或 `stream.config.load`：覆盖状态对象或 config 类错误。
- 禁止代表 action 继续返回弱 `ACTION_FAILED + message`。

验收：

- 新增/更新 unit test：ErrorBuilder 字段生成。
- 新增/更新 contract test：schema 层错误字段位于 `error`。
- 新增/更新 handler negative test：代表 action handler 错误有 `error_layer:"handler"`。
- 新增/更新 xout test：错误 xout 可见 example note、correct example、next actions。
- `make -C xdebug schema-test`
- `make -C xdebug contract-test`
- 若触发真实 FSDB/NPI，按 AGENTS.md 在沙箱外运行关联测试。

commit 边界：

- 只提交公共错误合同、代表 action 迁移和对应测试。

### 阶段 3 任务：MCP 入口错误合同

目标：让 MCP 入口在成功和失败路径都遵循当前入口心智模型，消除 native envelope 泄漏和 session 生命周期混淆。

任务：

- `xverif_debug_query(output_format:"xout")` 下 backend `ok:false` 渲染为 xout error。
- 保留结构化错误字段，便于 AI 自动修复。
- MCP wrapper 将 backend native `correct_example` 翻译为当前 MCP tool 参数形态。
- MCP wrapper 替换 `example_note`，明确不要把 native `api_version/target` 写进 MCP args。
- `xverif_debug_raw_request` 对 backend 参数错误进行结构化提升，不再只包进 `stdout_tail`。
- `xverif_debug_query` 禁止 native `session.*`。
- `xverif_debug_raw_request` 禁止 native `session.*`。
- 禁止 action 列表：
  - `session.open`
  - `session.close`
  - `session.kill`
  - `session.gc`
  - `session.doctor`
  - `session.list`
- session 禁止错误返回 `error_layer:"wrapper"`，`correct_example` 给 MCP session tool。
- MCP tool help / skill 文档同步说明 MCP session tools 是唯一生命周期入口。

验收：

- MCP unit tests 覆盖 query 错误 xout。
- MCP unit tests 覆盖 native example 翻译为 MCP example。
- MCP negative tests 覆盖 `session.open/close/list` 被拒绝。
- raw_request negative tests 覆盖 backend error 提升。
- `make -C xdebug mcp-test`
- 若 MCP stdio-loop/UDS 涉及 sandbox 差异，按 AGENTS.md 做沙箱外验证。

commit 边界：

- 只提交 MCP wrapper、MCP tests、相关 help/skill 小范围说明。

### 阶段 4 任务：参数词典与输出合同迁移

目标：统一全 action 的公共参数命名、输出对象、时间对象、config.list/export 语义，消除 AI 最常猜错的旧字段和同义字段。

任务：

- 全仓库 request schema 删除或拒绝 `limit`，迁移为 `line_limit`。
- 删除或拒绝旧数量字段：
  - `max_items`
  - `max_events`
  - `max_samples`
  - `max_findings`
  - `max_examples`
- 所有错误提示指向 `args.line_limit`。
- 统一 response summary：
  - `returned_count`
  - `line_limit`
  - `truncated`
  - `total_count` 或 `estimated_total_count` 如可得。
- 删除 public `include_*` 参数。
- 确认全体系只剩 `args.output.verbose` 一个 verbose。
- 统一 `args.output`：
  - `path`
  - `file_format`
  - `verbose`
- 拒绝旧输出字段：
  - `output.format`
  - `output_file`
  - `output_dir`
  - `output_prefix`
  - `rc_path`
  - 顶层输出 `path`
- 所有 export action 支持：
  - 有 `args.output.path`：写文件或目录。
  - 无 `args.output.path`：返回 response preview。
- 所有 export response summary 包含：
  - `status`
  - `output_written`
  - `output.path` 如适用
  - `row_count`
  - `truncated`
- 所有 `*.config.list` 支持：
  - `args:{}` 列全部。
  - `args.name` 展示单个详情。
  - 不存在返回结构化 `CONFIG_NOT_FOUND`。
- 时间合同统一：
  - request 单点只用 `args.time`。
  - request 窗口只用 `args.time_range.begin/end`。
  - 拒绝 `at/requested_time/start_time/begin/end/from/to` 请求别名。
  - xout 时间带单位，裸 tick 字段改名 `*_ticks`。
- query 形态统一：
  - `*.query` kind 可枚举。
  - object query 使用 `kind` 或 `type`。
  - schema 完整约束 `required` 和 `additionalProperties:false`。
  - 同协议族同名字段 enum 尽量统一。

验收：

- schema-test 覆盖新字段。
- contract-test 覆盖旧字段拒绝和 `did_you_mean`。
- runtime focused probes 覆盖代表 export/config.list/time/query。
- docs/examples 更新到新字段。
- `make -C xdebug schema-test`
- `make -C xdebug contract-test`
- 根据触及 action 选择 `make -C xdebug test-fast` 或更小 focused probes。

commit 边界：

- 参数词典和输出合同可拆为多个 commit，但每个 commit 必须包含 schema/runtime/tests/docs 的闭环。

### 阶段 5 任务：统一表达式组件和 alias 合同

目标：用一套表达式 evaluator 覆盖 stream/event/expr/window/verify/handshake，统一 alias、错误提示和 X/Z 语义。

任务：

- 从当前 stream `StreamExpression` 抽取或重构统一 evaluator。
- 能力基线：
  - signal / literal
  - bit select
  - concat
  - unary `!` / `~`
  - logical `&&` / `||`
  - bitwise `&` / `|` / `^`
  - compare `==` / `!=` / `>` / `>=` / `<` / `<=`
- 定义统一 expression result：
  - bits/value
  - known
  - width
  - status: `ok | unknown | error`
- 定义统一 expression error：
  - unknown alias
  - parse error
  - select out of range
  - unsupported operator
  - missing sampled value
- 通用 action 表达式只允许 alias。
- 真实 signal path 只出现在 `signals` map 或 config 绑定中。
- 迁移 `expr.eval_at`：
  - 输入 `args.signals + args.expr`。
  - 输出统一 expression result。
- 迁移 `event.find/export`：
  - 使用统一 evaluator。
  - inline 和 config mode 都使用 alias。
- 迁移 `verify.conditions`：
  - 输入 `args.signals + conditions[].expr`。
  - 拒绝旧 `conditions[].signal/op/value`。
- 迁移 `window.verify`：
  - 输入 `args.signals + conditions[].expr`。
  - 使用统一 pass/fail/unknown 语义。
- 迁移 `stream.*`：
  - stream config 加 `signals` map。
  - `clock/reset/vld/rdy/bp/sop/eop/data/channel_id/*_fields` 都引用 alias/expression。
  - `clock` 必须是 alias。
  - 拒绝旧直接 path config。
- 迁移 `handshake.inspect` 表达式入口，如存在 inline vld/rdy/data 表达式。
- `event.config.load` alias 化。
- AXI/APB config 不纳入 alias 化改造。

验收：

- expression unit tests 覆盖全部操作符。
- expression unit tests 覆盖 literal、select、concat、X/Z。
- expression negative tests 覆盖 unknown alias、parse error、select out of range。
- schema tests 覆盖 `verify.conditions/window.verify/stream.config.load/event.config.load` 新形态。
- runtime focused probes 覆盖：
  - `expr.eval_at`
  - `event.find`
  - `event.export`
  - `verify.conditions`
  - `window.verify`
  - `stream.config.load`
  - `stream.query`
  - `handshake.inspect` 如适用。
- `make -C xdebug schema-test`
- `make -C xdebug contract-test`
- `make -C xdebug test-fast` 或按真实 FSDB/NPI 规则沙箱外运行。

commit 边界：

- 表达式组件可先单独 commit。
- 全 action 迁移必须在同阶段完成后 commit，不留下双轨公共合同。

### 阶段 6 任务：action family 全面迁移

目标：把剩余 action 的 handler 层错误、empty/partial summary、xout 信息分层和必要 debug evidence 全部迁移到新合同。

批次 A：value/list/scope/signal

- `value.at` / `value.batch_at` / `list.value_at`：
  - signal/clock/time 错误使用 ErrorBuilder。
  - batch partial summary 统一计数。
  - correct_example 不回显坏 signal/clock/time。
- `list.create/add/delete/show/validate/export/diff`：
  - list not found 统一 `LIST_NOT_FOUND`。
  - index base 明确。
  - delete response 使用 `removed_index/removed_signal/count_after`。
  - export index 与 list index 避免冲突。
- `scope.roots/list`：
  - 空结果给 `empty_reason`。
  - 旧字段 `depth` 指向新字段或按最新合同拒绝。
- `signal.resolve/canonicalize/changes/statistics/stability`：
  - no-result 和 not-found 区分。
  - summary 第一屏统一。

批次 B：event/expr/window/verify

- 完成表达式统一后的错误和 response polish。
- event 表格 metadata 移出 row section。
- unknown alias 提供 `available_aliases`。
- malformed condition 不允许变成 pass/unknown。

批次 C：stream/handshake

- stream config/list/show/query/export/validate 统一错误。
- query kind enum 化。
- `match` 形态严格约束。
- verbose 输出避免超宽 union table。
- handshake rules 形态与 schema 对齐。

批次 D：trace/rc/source

- trace driver/load/active_driver/active_driver_chain：
  - signal not found 语义统一。
  - no path/no active evidence 用 `empty_reason`。
  - time 字段统一。
- rc.generate：
  - request/response 使用 `args.output.path` / `output.path`。
  - 删除 `rc_path/include_preview`。
- source.context：
  - 路径脱敏或 verbose 才显示绝对路径。
  - context 参数如支持则 schema 化，不支持则给明确错误。

批次 E：AXI/APB

- AXI/APB query/analysis/export/cursor/config list 等：
  - direction enum 统一或显式说明差异。
  - 重型 query 给缩窄建议。
  - xout time 带单位。
  - payload 默认截断，必要信息不丢。
  - config not found 不回显坏 name。
  - line_limit 控制返回行数。

验收：

- 每个批次至少跑对应 schema/contract/focused runtime tests。
- 每个批次更新 action-specific negative tests。
- 每个批次检查 xout golden 或 snapshot。
- 涉及真实 FSDB/NPI/VCS/VIP 的动作沙箱外执行。

commit 边界：

- 每个批次一个或多个 commit。
- commit message 写清 family、合同变化和验证命令。

### 阶段 7 任务：skill、架构文档和 mirror 收敛

目标：所有 agent-facing 文档都只描述新合同，避免 AI 从旧示例学习错误字段。

任务：

- 更新 `skills/xverif-cli`：
  - native envelope 示例全部使用新字段。
  - 不出现旧 `limit/include_*/output.format/rc_path/from/to/conditions[].signal/op/value`。
  - 表达式示例使用 alias map。
  - stream/event config 示例使用 alias map。
- 更新 `skills/xverif-mcp`：
  - MCP 示例使用 tool 壳。
  - `correct_example` 说明当前入口唯一。
  - MCP session 生命周期只能使用 MCP session tools。
  - 默认强调 xout；错误 xout 仍可读。
- 更新 `doc/agents/xdebug/`：
  - error contract。
  - output contract。
  - expression evaluator 架构。
  - 如何添加 action 时接入 ErrorBuilder 和 expression evaluator。
- 更新 README/help/json-api/action-reference/response-fields。
- 同步安装版 skill mirror：
  - `~/.codex/skills/xverif-cli`
  - `~/.codex/skills/xverif-mcp`
  - 如存在 Claude mirror，也同步。

验收：

- skill 文档 grep 检查旧字段不残留。
- 文档 JSON 示例 schema 校验。
- `git diff --check`
- mirror `diff -qr`。

commit 边界：

- 文档和 skill mirror 单独 commit。

### 阶段 8 任务：全量回归与远端交付

目标：在所有阶段完成后，用全量回归和 live MCP smoke 确认新合同闭环，然后推送远端。

任务：

- 清理本轮 disposable MCP session。
- 检查无本轮 xdebug-engine 残留。
- 跑源码关联测试：
  - `make -C xdebug schema-test`
  - `make -C xdebug contract-test`
  - `make -C xdebug unit-test`
  - `make -C xdebug test-fast`
  - `make -C xdebug mcp-test`
- 视修改范围跑更大回归：
  - `make -C xdebug test-regression`
  - VIP/NPI/VCS/LSF 相关目标按 AGENTS.md 沙箱外执行。
- 抽样 live MCP 调用：
  - 成功路径 xout。
  - schema 错误 xout。
  - handler 错误 xout。
  - session.* 禁止错误。
  - expression alias 错误。
- 检查 skill mirror。
- `git status --short` 确认干净。
- 按阶段 commit 全部完成后推送当前分支。

验收：

- 所有关联测试通过，或清楚记录环境阻塞。
- live MCP 错误提示能直接告诉 AI 正确入口和示例。
- 无 session/engine 残留。
- 远端 push 成功。

## 测试总要求

源码变更提交前必须跑通关联测试。常用入口：

- `make -C xdebug schema-test`
- `make -C xdebug contract-test`
- `make -C xdebug unit-test`
- `make -C xdebug test-fast`
- `make -C xdebug mcp-test`
- 涉及真实 FSDB/NPI/VCS/VIP/LSF 的回归按仓库规则在沙箱外运行。

新增/更新测试重点：

- 所有 `ok:false` 都有 `error_layer`。
- 迁移后的 handler 错误不再返回弱 `ACTION_FAILED + message`。
- `correct_example` 当前入口唯一，且不回显触发错误的坏值。
- MCP error xout 与 JSON 错误字段一致。
- 旧参数和旧形态被拒绝，并给结构化修复提示。
- 表达式 evaluator 覆盖 stream/event/expr/window/verify/handshake。
- compact xout 第一屏 summary 稳定，表格块不混 metadata。

## 风险

- 本计划包含破坏性 API 变更，需要 skill、docs、tests 和安装版 skill mirror 同步更新。
- 表达式组件一次性迁移范围大，需要防止 stream 现有能力回退。
- `line_limit`、`output.file_format`、stream/event alias 化会影响大量示例和用户习惯。
- MCP 禁止 native `session.*` 可能改变 raw_request 既有用途，但符合 MCP session 生命周期一致性目标。

## 回滚策略

不提供 runtime 兼容 fallback。若某阶段回归风险过高，应回滚该阶段 commit，而不是在 runtime 中加入旧字段兼容分支。

每阶段 commit 保持边界清晰，便于单独 revert。
