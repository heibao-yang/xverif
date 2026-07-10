# xdebug 全 action 评审问题修复执行计划

- 计划生成时间：2026-07-10 10:54:25（Asia/Shanghai）
- 输入报告：`doc/XDEBUG_ACTION_CLI_MCP_REVIEW_2026-07-10_10-09-13.md`
- 目标范围：报告覆盖的 70 个 xdebug public action，以及它们依赖的 xverif MCP session 公共基础设施
- 额外必要范围：xcov MCP session 生命周期对称化；不扩展为 xcov 全 action 整改
- 后续执行方式：以本文件作为 goal 模式的唯一工作计划入口，逐阶段实现、验证、提交，最终 clean 后执行全仓库测试并推送远端
- 当前状态：goal 执行中；阶段 0 基线已确认，尚未开始源码实现

## 0. 执行基线

- 基线分支：`master`
- 基线提交：`51b11b7 文档：重新评审 xdebug action 返回可用性`
- 基线 runtime catalog：70 个 implemented action、1 个 removed action（`signal.search`）
- 基线工作树：仅本轮评审报告和修复计划为未跟踪文件，无源码改动
- 基线验证：runtime `actions` 原生 JSON 请求成功；计划矩阵覆盖报告中的 70/70 action；`git diff --check` 通过
- 阶段 0 限制：未调用仓库内 70-action replay runner；后续逐 action 验收使用独立合同测试矩阵

## 1. 已确认的关键决策

本计划固定以下决策，后续实现不得自行改回兼容模式或引入 fallback：

1. 采用合同优先方案：先统一公共错误、状态、时间、输出和扫描覆盖合同，再迁移各 action。
2. 请求所依赖的 signal、scope、config、list、cursor、session 等资源不存在时，顶层返回 `ok=false` 和明确的 `*_NOT_FOUND`；资源存在但合法查询结果为空时才返回 `ok=true`、`count=0`。
3. 默认输出为 compact；详细证据只通过已有的 `args.output.verbose=true` 展开，不新增 action 私有的 `include_*` 或同义开关。
4. `stream.export` 的 `line_limit` 只允许用于无 `output.path` 的 response preview；写文件请求同时携带 `line_limit` 时由 schema 直接拒绝。文件导出完整的过滤后结果，不静默忽略参数。
5. MCP session 采用 `5A1-Y`：完善现有 `McpSessionManager`，由固定 native administrative control path 管理 MCP 自己创建的 backend；这条路径是正式控制面，不是失败后的 fallback。
6. xdebug 与 xcov 提供对称 MCP 生命周期工具和返回合同，通过 backend capability 适配各自差异。
7. 禁止 `xverif_cov_query` 像普通 coverage 查询一样直接调用 native `session.open/status/close`；必须使用专用 MCP session 工具。
8. 所有时间输出统一通过 `time_contract`：默认 ns，按请求渲染单位，只返回无精度损失的字符串，不新增并行的无单位数字时间。

## 2. 总体目标与完成标准

完成本计划后，应同时满足以下条件：

- 70 个 xdebug action 的正确、schema 错误、handler/语义失败三类返回都有明确、稳定、可测试的合同。
- CLI native JSON 与 MCP JSON 的 action 核心语义一致；MCP 只负责参数壳、session ownership 和必要的示例转换。
- JSON 与 XOUT 对同一 action 的状态、计数、时间、截断范围和 verdict 一致。
- 同义字段不再因 action 不同而改变含义，例如 `line_limit`、`truncated`、`found`、`healthy`、`verdict`、`clock_edge_hit`。
- 所有 not-found 都能区分“资源不存在”和“资源存在但结果为空”。
- 所有分析类 action 都明确扫描范围、是否完整、截断作用域；不允许用截断后的证据推断未扫描范围。
- schema 和 handler 错误均可直接用于修复请求，`correct_example` 必须 schema-valid 且不得复制原始坏值、坏路径或本机绝对路径。
- 默认 compact 返回足以支持下一步 debug 决策，但不重复整块事务、sample、preview 或源码证据。
- xdebug/xcov managed session 的 open/list/doctor/close/kill/gc 工具对称，返回明确 ownership 和分层清理结果。
- 所有关联文档、examples、help、skills 与 runtime/schema 同步。
- 最终从 clean 工作区构建后，顶层 `make test` 与 `make full-test` 均通过；要求运行的沙箱外 EDA/MCP/LSF 测试不得用低层级测试替代。
- 所有计划内 commit 均为中文详细提交说明；最终工作树确认无意外文件后推送当前目标分支到 `origin`。

## 3. 明确不做的事项

- 不引入 transport、backend、数据源、旧字段别名、旧行为或测试层级的静默 fallback。
- 不通过保留两套字段来兼容矛盾语义；错误合同直接修正并同步 schema/examples/tests。
- 不新增 `xdebug.v2`；本轮将已确认的错误或歧义作为 `xdebug.v1` 合同修复。
- 不把 MCP managed session 与 CLI native session 合并成同一个虚假列表。
- 不允许 MCP gc 默认杀死仍存活但 unhealthy 的 session；必须由显式 kill 决定。
- 不扩展为 xcov 查询结果、coverage 语义或报告格式的全面重构。
- 不使用仓库中的 70-action replay runner 作为实现或验收捷径；逐 action 合同测试应是可定位的独立测试或参数化测试矩阵。

## 4. 公共合同设计

### 4.1 成功、失败与业务 verdict

统一规则：

- `ok=true`：请求被完整执行，依赖资源存在，返回结果可信。
- `ok=false`：schema、参数、资源、环境、session、执行或内部错误。
- `summary.status`：action 业务状态，例如 `found`、`written`、`preview`、`completed`。
- `summary.verdict`：只用于 verify/validate 一类业务判断，例如 `pass`、`fail`、`inconclusive`。
- `summary.execution_ok`：对可能出现业务失败 verdict 的 action 显式表示计算是否完整执行。
- `verify.conditions` 的条件不满足属于 `ok=true, execution_ok=true, verdict=fail`；表达式无法求值、资源缺失或扫描不完整到无法给出 verdict 时为 `ok=false` 或 `verdict=inconclusive`，不得伪装为 pass/fail。

not-found 统一分类：

- `SIGNAL_NOT_FOUND`
- `SCOPE_NOT_FOUND`
- `CONFIG_NOT_FOUND`
- `LIST_NOT_FOUND`
- `CURSOR_NOT_FOUND`
- `SESSION_NOT_FOUND`
- `SOURCE_NOT_FOUND`
- 其它 action-specific resource code 必须落入相同结构

资源存在但没有 driver/load/event/transaction/difference 时返回 `ok=true`，并在 summary 中给 `found=false`、明确的结果计数和原因。

### 4.2 错误结构

所有 schema、handler、wrapper、session manager、transport 错误统一保留：

```json
{
  "ok": false,
  "summary": {
    "status": "error",
    "error_code": "CONFIG_NOT_FOUND"
  },
  "error": {
    "code": "CONFIG_NOT_FOUND",
    "message": "...",
    "error_layer": "handler",
    "recoverable": true,
    "invalid_arg": "args.name",
    "expected": "...",
    "allowed_values": [],
    "correct_example": {},
    "next_actions": []
  }
}
```

约束：

- 完整错误细节只放 `error`；`summary` 不复制 `invalid_arg/expected/correct_example`；失败时 `data` 不复制 error。
- `error_layer` 只能取 `schema`、`handler`、`wrapper`、`session_manager`、`transport`、`internal`。
- MCP schema 拒绝使用 `mcp_schema` 或现有 MCP 规范层名时，要在公共映射中形成明确的一一对应，不与 native schema 混淆。
- `correct_example` 从 checked-in valid example 或 action-specific valid builder 生成；禁止从失败 request 做浅拷贝。
- MCP 只把 native envelope example 转成正确的 MCP tool 参数壳，不改 action 内层语义。
- `did_you_mean` 只有候选值与原值不同时才出现；缺失 `args.stream` 时不得建议 `args.stream` 本身。
- validator 应收集同一层的可独立报告问题，例如缺失字段与 unknown field；不得永远只暴露第一个错误而隐藏明显的并列问题。

优先复用和扩展：

- `xdebug/src/api/diagnostic_error.*`
- `xdebug/src/engine/service/engine_action_handler.*`
- `xdebug/src/core/schema/runtime_schema_validator.*`
- 现有 waveform/design/protocol/list/event domain error helpers

domain helper 只能补 action-specific expected/next_actions，不得自行重建公共 error envelope。

### 4.3 summary/data/findings 分工

- `summary`：状态、计数、关键时间范围、峰值、verdict、是否完整、是否截断。
- `data`：用于复现结论的 rows、transactions、paths、checks、mapping 或 artifact manifest。
- `findings`：按严重度组织的异常证据；不得与 `data` 或 `summary.first_*` 完整重复。
- `warnings`：不阻止 action 完成的限制或质量提示。
- `meta`：版本、耗时、请求追踪等非业务字段。

禁止默认出现的重复形态：

- `values + sample_rows + samples` 表示同一组值。
- `events + preview` 表示同一组事件。
- `summary.first_transfer + data.row` 表示同一事务。
- `first_risk + findings[0]` 表示同一 finding。
- `implemented[] + actions[].name` 表示同一 action catalog。

### 4.4 compact/verbose 投影

统一使用 `args.output.verbose`：

- 默认 `false`：返回 debug 决策所需最小证据。
- `true`：返回完整 beat payload、source snippets、sample bracket rows、per-cycle timeline、internal session diagnostics 等。
- compact/verbose 选择必须在 handler 公共投影层完成，JSON 和 XOUT 消费同一个投影结果。
- 不在 XOUT renderer 内重新猜测 compact 语义。

需要公共投影 helper 的数据族：

- AXI/APB transaction/beat projection
- clock sample/bracket projection
- stream/event row projection
- source/path evidence projection
- cursor/session public projection

### 4.5 时间与 clock sampling

- 所有 public time、latency、begin/end、first/last/peak time 使用 `time_contract` 字符串。
- 默认单位 ns；`args.time_unit` 仅影响输出渲染，不改变解析、比较、排序或采样逻辑。
- clock context 统一字段：`requested_edge`、`requested_edge_hit`、`any_edge_hit`、`actual_edge_kind`、`previous_sample_time`、`sample_time`、`next_sample_time`、`bracket_complete`。
- `value.at`、`value.batch_at`、`list.value_at`、`verify.conditions`、`expr.eval_at` 必须共享 `ClockPointSampler/ClockSampleTimeResolver` 的同一 bracket 算法。
- 建立同 fixture、同 clock、同 time、同 edge 的跨 action golden matrix，禁止 action wrapper 自行修补 bracket。

### 4.6 扫描覆盖与截断

所有会扫描波形、事务或样本的 action 统一返回：

- `requested_range`
- `scanned_range`
- `analysis_complete`
- `truncated`
- `truncation_scope`，取 `response_rows`、`findings`、`analysis_samples`、`export_preview` 等明确值
- `returned_count`
- 能计算时返回 `total_count`

规则：

- `line_limit` 只限制 public response rows/items/findings，不得无声明地限制内部正确性分析。
- 如果资源上限导致分析不完整，不得对未扫描范围给出 finding 或 pass verdict。
- action 可以返回部分证据，但必须将 `analysis_complete=false` 和未覆盖范围放入 summary。
- `stream.export` 写文件模式不接受 `line_limit`；preview 模式才使用它。

### 4.7 effective config

APB、AXI、event、stream config action 统一：

- `*.config.load` 成功返回 load status、name/count、warning 和可核验的 effective config；批量 stream load 可返回名称列表并明确建议 show/validate，避免一次展开全部大型配置。
- `*.config.list(name=...)` 返回完整 effective mapping。
- 空参数 list 默认只返回 name/status 的 compact catalog。
- config path 错误必须精确指向 `args.config_path`。
- 配置加载成功不等于动态信号可解析；响应要给明确的 validate/show 下一步。

## 5. MCP session 生命周期设计

### 5.1 公共状态机

在现有 `McpSessionManager` 和 loop session 上统一状态：

- `new`
- `opening`
- `alive`
- `unhealthy`
- `closing`
- `closed`
- `dead`
- `cleanup_partial`
- `orphan_suspected`

manager 同时保存 active record 和 tombstone。发生 transport、IO、backend terminal 或 partial cleanup 时不得立即丢弃唯一恢复证据。

每条 public record 必须带：

- `alias`
- `session_id`
- `ownership=managed`
- `backend=xdebug|xcov`
- `launcher=direct|lsf`
- `state`
- compact 模式下的 resource basename/hash
- verbose 模式下的 PID、job、完整资源路径和分层诊断

### 5.2 对称 MCP 工具

xdebug：

- `xverif_debug_session_open`
- `xverif_debug_session_list`
- `xverif_debug_session_doctor`
- `xverif_debug_session_close`
- `xverif_debug_session_kill`
- `xverif_debug_session_gc`

xcov：

- `xverif_cov_session_open`
- `xverif_cov_session_list`
- `xverif_cov_session_doctor`
- `xverif_cov_session_close`
- `xverif_cov_session_kill`
- `xverif_cov_session_gc`

公共参数规则：

- 单 session 操作必须提供精确 `name` 或 `session_id`。
- MCP kill 不支持隐式当前 session，也不支持 `all`；批量清理由调用者显式 batch 多次操作。
- list 支持 `include_tombstones=false` 与 `verbose=false`。
- doctor 支持 `verbose=false`，只读且不得自动重启、重连或 reopen。
- gc 默认仅清理已确认 closed/dead/stale 的 managed record/tombstone；alive 但 unhealthy 的 session 只报告为 unresolved。

### 5.3 backend capability

公共 manager 不使用 `if backend == ...` 散落判断，而定义 backend lifecycle capability：

| 能力 | xdebug | xcov |
| --- | --- | --- |
| native open | `session.open` | `session.open` |
| native health | `session.doctor` | `session.status` |
| native close | `session.close` | `session.close` |
| native kill | `session.kill` | 无；终止 loop 即终止 backend |
| native gc | `session.gc`，仅用于该 manager 拥有的 session | 无 |
| dead-loop backend | detached engine 可能存活 | backend 随 loop 退出 |

xdebug 的 fixed administrative control path：

- alive 时沿现有 stdio-loop 调 native doctor/close/kill。
- loop 已死且 detached engine 可能残留时，使用明确、固定、可审计的 native admin path 按已保存 backend session ID 执行 doctor/kill。
- admin path 的调用结果必须进入 cleanup stages；不得失败后切换其它 transport。
- 无法确认 native cleanup 时状态为 `orphan_suspected` 或 `cleanup_partial`，保留 tombstone。

xcov：

- doctor 内部调用 native `session.status`。
- close 调 native `session.close` 后退出 loop。
- kill 终止 loop/direct process/LSF job；不得虚构 native kill 响应。
- loop 已死时 coverage backend 已随进程退出，manager 只需要确认 process/job cleanup 并维护 tombstone。

### 5.4 分层清理结果

close/kill/gc 统一返回：

```json
{
  "summary": {
    "status": "closed",
    "ownership": "managed",
    "cleanup_complete": true
  },
  "data": {
    "cleanup": {
      "native_backend": "closed",
      "stdio_loop": "quit",
      "process": "terminated",
      "lsf_job": "not_applicable",
      "manager_record": "evicted",
      "tombstone": "not_needed"
    }
  }
}
```

任一阶段失败：

- 顶层 `ok=false`。
- error code 为 `SESSION_CLEANUP_PARTIAL_FAILURE` 或更精确的 session code。
- `error_layer=session_manager`。
- 保留每阶段结果和 tombstone。
- 不将 native response 的“收到返回”误判为业务 `ok=true`；必须检查返回 envelope。

### 5.5 禁止绕过 manager

- xdebug 继续禁止 `xverif_debug_query` 直接调用 native `session.*`。
- xcov 新增相同 guard，禁止 `xverif_cov_query` 直接调用 native `session.open/status/close`。
- wrapper 错误提供对应专用 MCP tool 的正确示例。
- SDK-free loop wrapper 的 `debug.session.*` 与 `cov.session.*` 同步增加 doctor/kill/gc，不能只更新 MCP SDK server。

## 6. 分阶段实施计划

### 阶段 0：建立基线与变更台账

工作项：

- 保存当前 70 action runtime catalog、request schema 和关键 response fixture 的基线摘要。
- 为报告中的每个问题建立 test ID，并映射到本文件第 7 节 action 矩阵。
- 记录当前 `git status --short`，保护用户已有的 review report 和其它无关变更。
- 确认真实 fixture、VIP、NPI、direct MCP、fake LSF、real LSF 的可用条件。
- 不运行 70-action replay runner。

退出条件：所有 70 action 都有计划归属；关键正确性问题都有先失败的测试设计。

### 阶段 1：公共 error、example、status 与 projection 合同

工作项：

- 扩展 ErrorBuilder/ResponseBuilder，统一 error layer 和 summary/data/error 分工。
- 建立 valid example provider，接入 runtime schema validator 和 handler helper。
- 修复同值 `did_you_mean`、坏值复制、绝对路径泄漏和并列 schema issue 收集。
- 建立 not-found/status 公共 helper。
- 建立 compact/verbose response projection helper，确保 JSON/XOUT 共用投影。
- 给公共合同添加 unit、schema、contract 测试和静态 consolidation guard。

退出条件：新迁移 action 不再自行拼装公共 envelope；错误 example 可过对应 request schema。

### 阶段 2：最高优先级正确性修复

工作项：

- 修复 `value.at` 与 batch/list/verify 的 clock bracket 分歧。
- 修复 `sampled_pulse.inspect` 使用截断 edge 推断后续 finding 的问题。
- 修复 `signal.changes` 默认不返回 timeline rows；统一 change row 与 actual transition 计数。
- 修复 `signal.canonicalize/resolve`、`trace.driver/load`、`scope.list/roots` 的 not-found/resource-availability 语义。
- 修复 `expr.normalize` 对明显无效表达式仍成功的问题。

退出条件：所有会造成错误 debug 结论的假阳性或越界推断先被消除。

### 阶段 3：waveform/list/event/cursor 输出合同

工作项：

- 引入扫描覆盖、截断作用域和统一 count 字段。
- 精简 values/sample_rows/samples、events/preview、first_risk/findings 等重复。
- 补齐 signal stability/statistics、handshake、detect_abnormal、window/verify 的证据范围。
- 补齐 list/cursor 的 summary、not-found、恢复建议和 compact 元数据。
- `source.context` 默认返回小型源码窗口；verbose 扩大窗口。
- `rc.generate` 返回 missing/skipped signal 摘要。

退出条件：每个 compact response 都能直接回答“发生了什么、在哪里、证据是否完整、下一步是什么”。

### 阶段 4：APB/AXI/stream 协议合同

工作项：

- APB/AXI load/list 返回完整 effective config mapping。
- 所有 APB/AXI transaction time、latency、cursor time 走 time_contract。
- AXI transaction 默认 compact，beat data/wstrb 仅 verbose 展开。
- outstanding timeline 默认峰值+变化点，逐周期 rows 放 verbose。
- analysis 增加 latency unit、p50/p95/p99、最慢事务锚点。
- stream query/validate/export 按查询类型裁剪，补 scanned range 和 truncation scope。
- 实施 `stream.export` preview/write 的 schema 互斥合同。

退出条件：三类协议 action 在 config、time、payload、coverage 上使用相同语义。

### 阶段 5：xdebug/xcov MCP session 对称生命周期

工作项：

- 重构 shared manager 状态机、backend capability、tombstone 和 cleanup result。
- 增加 debug/cov doctor/kill/gc MCP tools 和 SDK-free wrapper method。
- xdebug 实施 fixed native administrative path。
- xcov doctor 映射 native status；kill/gc 按 loop-owned backend 实现。
- coverage query 增加 native lifecycle guard。
- 修复 `include_native` 无效参数：删除该公开参数，不做兼容别名；native list 仍由 CLI action负责。
- 默认 session public JSON compact，敏感/噪声字段放 verbose。

退出条件：direct、fake LSF、real LSF 条件下的正常、dead loop、native close fail、partial cleanup、tombstone、gc 均有明确测试。

### 阶段 6：catalog、schema、XOUT、docs、examples、skills 收口

工作项：

- actions 默认 compact catalog，verbose 返回 descriptor；去掉 implemented/name 重复。
- schema unknown action 返回相近候选。
- batch 汇总 failed indexes/codes/layers。
- 更新 70 action request/response examples、help、README、`doc/agents/xdebug`。
- 更新 repo 内 `skills/xverif-cli` 与 `skills/xverif-mcp`；实现完成并验证后再按 Makefile 安装到 Codex/Claude。
- 更新 MCP tool catalog、README、stateful session reference、SDK-free protocol 文档。

退出条件：runtime、schema、examples、help、skills 对相同字段给出一致描述。

### 阶段 7：最终 clean、全仓库验证、提交收口与 push

工作项见第 9、10、11 节。任何 required test 失败都阻止最终 push。

## 7. 逐 action 修复矩阵

优先级：P0=可能给出错误 debug 结论；P1=恢复性或关键证据不足；P2=冗余、一致性或易用性问题。

### 7.1 Protocol/config actions

| Action | 优先级 | 修复内容 | 验收重点 |
| --- | --- | --- | --- |
| `apb.config.load` | P1 | 回显完整 APB effective mapping；修正 `args.config_path`；公共 error projection | inline/path 成功可核验全部信号；missing path example 有效 |
| `apb.config.list` | P1 | name 查询返回完整 mapping；空查询 compact names；补 summary | 与 load/show 的 mapping 一致 |
| `apb.query` | P0 | transaction time 改为 time_contract 字符串；清理失败重复 | 与 transfer_window 同 fixture 时间一致 |
| `apb.cursor` | P1 | 标准时间、1-based index、total count | begin/next/prev 边界可判断 |
| `apb.transfer_window` | P1 | returned/total/truncated/scanned_range；有效 missing config example | 截断不再伪装全量 |
| `axi.config.load` | P1 | 回显 AW/W/B/AR/R mapping；修正 config_path | load 成功足以核验完整绑定 |
| `axi.config.list` | P1 | 完整 channel mapping；补 summary/compact catalog | 与 stream config 可解释性对齐 |
| `axi.query` | P0 | time 字符串；默认 compact beat；verbose 完整 payload | 宽总线 compact 无全宽数据泛滥 |
| `axi.cursor` | P1 | 时间、index、total count | cursor 位置和边界明确 |
| `axi.request_response_pair` | P1 | 默认保留 ID/address/阶段时间/latency/missing；beat verbose | 配对根因不被 payload 淹没 |
| `axi.latency_outlier` | P1 | summary count/max；transaction compact；payload verbose | 最慢事务可直接定位 |
| `axi.outstanding_timeline` | P1 | peak_read/write/time、first_nonzero、变化点压缩 | compact 不要求遍历逐周期 rows |
| `axi.analysis` | P1 | latency 字符串或明确 duration；p50/p95/p99/slowest anchor | 量级与最慢根因可解释 |
| `axi.channel_stall` | P1 | scanned_range、first_activity、analysis_complete | 大窗口截断原因清楚 |
| `axi.export` | P2 | 覆盖/覆盖写策略；解释 requested 与 scan end 差异 | manifest 精简且覆盖范围明确 |

### 7.2 Design/trace/discovery actions

| Action | 优先级 | 修复内容 | 验收重点 |
| --- | --- | --- | --- |
| `trace.active_driver_chain` | P2 | 迁移公共 error helper，保持链路证据 | 不降低现有高质量返回 |
| `trace.active_driver` | P1 | missing signal 补 invalid_arg/expected/next_actions | 与 chain 错误质量一致 |
| `trace.driver` | P0 | 先验证 signal existence；区分 not-found/no-driver；source verbose | 不存在信号顶层失败 |
| `trace.load` | P0 | 区分 not-found/no-load；paths compact/source verbose | 空影响面不与拼错信号混淆 |
| `scope.roots` | P1 | resource_available、analysis_complete、limitations 提升 summary | design 未加载不被理解为无 root |
| `scope.list` | P0 | 不存在 path 返回 `SCOPE_NOT_FOUND`；合法空 scope 成功 | 两种空结果可区分 |
| `signal.resolve` | P0 | not-found 顶层失败；唯一/多匹配 status 统一 | 不再出现顶层 ok 与内部 ok 冲突 |
| `signal.canonicalize` | P0 | 禁止以原输入伪装 canonical；增加 found/status | 拼错路径不会被确认 |
| `source.context` | P1 | compact 默认返回小窗口源码；verbose 扩大；summary 一致 | action 真正提供 source context |
| `expr.normalize` | P0 | 返回真实 parsed/issues；明显语法错误失败 | string fallback 不再证明表达式有效 |

### 7.3 Session/meta actions

| Action | 优先级 | 修复内容 | 验收重点 |
| --- | --- | --- | --- |
| `session.open` | P1 | native/managed ownership、关键 summary；MCP shared contract | CLI/MCP 明确各自资源对象 |
| `session.list` | P1 | native 与 managed 分开；MCP compact/tombstone | 默认不暴露 PID/绝对路径 |
| `session.doctor` | P1 | 增加 MCP debug/cov 专用 tool；分层 health | 诊断只读、无 fallback |
| `session.close` | P0 | 检查 native response `ok`；partial cleanup+tombstone | backend 失败不得误报关闭成功 |
| `session.kill` | P1 | MCP 精确单 session tool；xdebug/xcov capability | 不支持隐式/all；清理层级明确 |
| `session.gc` | P1 | MCP managed gc；alive unhealthy 只报告 | 不误杀、不清理他人 native session |
| `schema` | P2 | unknown action 候选；保持精确 schema/path | CLI/MCP 专用 schema 核心一致 |
| `actions` | P2 | compact catalog；verbose descriptor；消除名字重复 | 默认响应显著缩小且仍可发现能力 |
| `batch` | P1 | failed indexes/codes/layers；顶层 error_layer | 不下钻即可定位失败 child |

### 7.4 Value/verify/signal analysis actions

| Action | 优先级 | 修复内容 | 验收重点 |
| --- | --- | --- | --- |
| `value.at` | P0 | 统一 ClockPointSampler bracket | 与 batch/list/verify 同 fixture 完全一致 |
| `value.batch_at` | P1 | 删除三套重复 sample；明确 requested/any edge | compact 保留 values+clock context |
| `verify.conditions` | P0 | execution_ok/verdict 双层状态；XOUT 顶部突出 FAIL；去重复 | 条件 fail 不与执行错误混淆 |
| `window.verify` | P1 | 回显 proof begin/end/scanned_range；有效 example | verdict 带完整证明范围 |
| `signal.changes` | P0 | 默认返回 line_limit 内 timeline rows；aggregate_only 显式聚合 | returned rows 与实际 data 一致 |
| `signal.stability` | P0 | change_row_count/actual_transition_count；summary stable/value | 恒定初始值不计作 transition |
| `signal.statistics` | P2 | 迁移有效 example 和扫描覆盖合同 | 保持现有统计价值 |
| `sampled_pulse.inspect` | P0 | 内部完整 edge index 与 response limit 分离；禁止越界 finding | truncated 后不引用陈旧 edge |
| `counter.statistics` | P2 | 有效 clock example；统一 scan coverage | 统计与时间范围保持可解释 |
| `handshake.inspect` | P1 | stall/ready-without-valid 时间锚点和窗口 | 异常可直接跳到波形现场 |
| `detect_abnormal` | P1 | 逐信号 scan status、规则、阈值、未命中原因 | 无 finding 可证明已检查 |
| `expr.eval_at` | P1 | requested_edge_hit/any_edge_hit；alias 错误候选；去重复 | edge miss 不与 any-edge hit 混淆 |

### 7.5 Event/list/cursor/stream/artifact actions

| Action | 优先级 | 修复内容 | 验收重点 |
| --- | --- | --- | --- |
| `event.config.load` | P1 | 精确 config_path；有效 load example；公共错误投影 | example 可直接通过 schema |
| `event.config.list` | P2 | summary name/clock/edge；保留已有 list/load next_actions | 不重复实现已存在恢复提示 |
| `event.find` | P1 | unknown alias 指向 args.expr、列 available aliases/example | 表达式失败可立即修复 |
| `event.export` | P1 | preview/output_written 明确；events/preview 单一表示 | preview 不被误认成文件产物 |
| `list.create` | P1 | 回显 signal_count/compact members；冲突 next_actions | 初始成员是否入表可确认 |
| `list.add` | P2 | 迁移公共错误投影 | 保持现有清晰成功合同 |
| `list.delete` | P2 | 明确 1-based index schema/help；公共错误投影 | exact/index 行为一致 |
| `list.show` | P2 | 清理失败重复 | 保持 one-based index 事实入口 |
| `list.validate` | P2 | 统一 not-found/scan status | 保持逐成员状态 |
| `list.value_at` | P1 | 删除 values/sample_rows/samples 重复；统一 bracket | 多信号证据 compact |
| `list.diff` | P1 | changed_signals、before/after values | 首差原因可直接判断 |
| `list.export` | P2 | 明确覆盖/冲突策略和 manifest | 不回灌二进制内容 |
| `cursor.set` | P1 | 有效 time example；summary name/time/status；epoch verbose | 坏时间示例不再复制 |
| `cursor.get` | P1 | summary name/time；CURSOR_NOT_FOUND 结构化 | list/set 下一步明确 |
| `cursor.use` | P1 | summary active/time；候选 cursor 与 next_actions | 切换和失败都可恢复 |
| `cursor.delete` | P1 | summary；明确非幂等；not-found helper | 删除不存在名称稳定失败 |
| `cursor.list` | P2 | summary count/active；空字段/epoch verbose | compact 列表足够操作 |
| `stream.config.load` | P1 | 精确 config_path；load 后 show/validate 提示；错误去重复 | load 成功不等同 dynamic valid |
| `stream.config.list` | P2 | 空查询 names，name 查询完整 config | 兼顾发现和核验 |
| `stream.show` | P2 | 清理同值 did_you_mean 和失败重复 | 保持完整静态语义说明 |
| `stream.query` | P1 | 按 query 类型裁剪；目标 row 不与 summary 重复；truncation scope | first_transfer 只返回一次 |
| `stream.validate` | P0 | validation_complete、scanned_range、truncation scope | partial validate 不得声称全量健康 |
| `stream.export` | P0 | preview/write schema 互斥；written summary 去大型样本 | `line_limit` 不再被静默忽略 |
| `rc.generate` | P1 | missing/skipped signals、产物摘要；有效 config example | 产物完整性可核验 |

## 8. 测试设计

### 8.1 公共合同测试

新增或扩展：

- schema validation：每个 action 正例、unknown field、missing required、互斥字段。
- correct example validation：从任意 schema/handler error 提取 example 后必须通过对应 request schema；资源型 example 不要求真实资源存在，但不得保留原始坏值。
- error shape：每层 error_layer、字段唯一位置、无 summary/data/error 重复。
- not-found matrix：signal/scope/config/list/cursor/session/source。
- compact/verbose matrix：compact 必备字段、verbose 只增加细节、不改变业务结论。
- JSON/XOUT parity：状态、verdict、计数、时间、扫描范围一致。
- time contract：默认 ns、指定单位、极大/极小时间、精度不丢失、无裸 numeric public time。
- scan coverage：完整、response truncated、analysis truncated、preview truncated。

### 8.2 跨 action golden tests

- 同 clock/time/edge：`value.at`、`value.batch_at`、`list.value_at`、`verify.conditions`、`expr.eval_at`。
- 同 signal/window：`signal.changes`、`signal.stability`、`signal.statistics`。
- 同 config：APB/AXI load 与 list effective mapping。
- 同 transaction：APB query/cursor/window 时间；AXI query/cursor/outlier/pair 时间和 compact payload。
- 同 alias error：event/stream expression action 的 available aliases 和字段定位。
- 同 not-found：resolve/canonicalize/trace/scope 的顶层失败语义。

### 8.3 MCP session unit/contract tests

xdebug 和 xcov 都覆盖：

- open/list/query/doctor/close 正常路径。
- alias 与 backend session ID 双索引。
- duplicate alias、missing resource、missing session。
- native close 返回 `ok=false` 但 transport 正常。
- stdio timeout、invalid JSON、process exit、backend terminal response。
- close partial failure 后 record/tombstone 保留。
- kill exact session；拒绝缺 key 和 `all`。
- gc closed/dead/stale；alive unhealthy 进入 unresolved。
- compact list 不含 PID/绝对路径；verbose 包含诊断字段。
- xdebug dead loop + detached backend 的 fixed admin cleanup。
- xcov dead loop 不执行不存在的 native kill。
- direct 与 fake LSF 行为一致；real LSF 在真实环境验证。
- MCP server tool catalog 与 SDK-free wrapper method 对称。
- `xverif_debug_query` 和 `xverif_cov_query` 都拒绝 native lifecycle action，并给正确专用工具 example。

### 8.4 逐阶段测试门禁

每个实现阶段提交前至少执行：

1. `git diff --check`
2. 该阶段新增的目标 pytest/unit tests
3. `make -C xdebug schema-test`
4. `make -C xdebug contract-test`
5. `make -C xdebug unit-test`
6. session/MCP 变更额外执行 `make -C xdebug mcp-session-test`、`make -C xdebug mcp-sdk-test`
7. xcov 变更额外执行 `make -C xcov test`

凡会启动 xdebug engine、访问真实 FSDB/NPI、运行 VCS/VIP、真实 MCP stdio-loop、UDS/LSF 的命令按 AGENTS.md 直接在沙箱外运行。沙箱内失败不能作为产品回归结论。

## 9. 最终 clean 与全仓库测试计划

最终验证必须从 clean 开始，不能只复用增量构建结果。

### 9.1 前置检查

- `git status --short`
- `git diff --check`
- 确认只包含计划内源码、schema、tests、docs、skills 和两份计划/评审文档。
- 确认没有测试产物、临时导出、FSDB、日志、session manifest 被加入 git。
- 记录 VCS/Verdi/NPI/VIP、license、LSF 和真实 fixture 可用性；不打印敏感配置。

### 9.2 clean

执行仓库真实入口：

```bash
make clean
```

随后检查构建产物确已删除，再开始全量构建和测试。

### 9.3 required 全仓库验证

以下命令均为 required，按环境规则在沙箱外运行：

```bash
make test
make full-test
```

说明：

- `make test` 覆盖 xdebug schema/contract/unit/MCP、xbit、xentry、xloc、xcov、xwaveform、active-driver fixture 和常规 xdebug regression。
- `make full-test` 从完整回归入口执行 clean build、unit、active-driver、design/waveform/VCS/NPI/真实数据条件项。
- `run_full_regression.sh` 当前允许对缺失外部 fixture 记 SKIP；goal 执行时必须逐项审查 SKIP。由本轮变更直接依赖且环境应提供的项目不得以 SKIP 当通过。
- 任一 FAIL 阻止最终提交收口和 push。

### 9.4 session/VIP/LSF 加强验证

由于本轮直接修改 MCP session 和 APB/AXI 行为，还必须在相应真实环境执行：

```bash
make -C xdebug test-nightly
```

若真实 LSF 环境已配置，执行：

```bash
XDEBUG_ENABLE_REAL_LSF=1 make -C xdebug test-mcp-real-lsf
```

规则：

- 不把 fake LSF 当 real LSF 的 fallback。
- 如果真实 LSF 是当前交付环境的 required 能力但不可用，停止 push 并报告阻塞。
- 如果仓库将其定义为 optional 且当前环境未配置，可明确记录 NOT RUN；不得写成 PASS。
- APB/AXI VIP、NPI、VCS 仿真全部直接在沙箱外运行。

### 9.5 最终 action 审计

- 重新查询 runtime `actions`，确认 70 个 xdebug action 未意外丢失。
- 对每个 action 重新执行正确、schema 错误、handler/语义失败测试，但使用本轮独立测试矩阵，不使用 70-action replay runner。
- CLI 与 MCP 比较 code、layer、status、summary 关键字段、时间和 truncation semantics。
- 对不可构造 handler 失败的 action 保留明确说明，不用 wrapper/session 错误冒充。
- 验证 xdebug/xcov MCP lifecycle tools 全部出现在 tool catalog 和 MCP SDK inspect 结果中。

## 10. Commit 计划

每个 commit 前必须：

- 运行该阶段 required tests。
- 执行 `git status --short`。
- 显式 `git add <files...>`；不使用 `git add .`。
- 确认不包含用户或其它进程的无关改动。
- commit subject/body 使用中文，写清动机、范围和验证。

建议 commit 切分如下；实现时可在不破坏依赖和可回滚性的前提下微调文件边界，但不得把未验证阶段混入提交：

1. `计划：记录全 action 合同修复与全量验证方案`
   - 加入原始评审报告和本计划。
   - 文档 only，执行链接、action 覆盖、格式检查。

2. `重构：统一 xdebug 错误状态与输出投影合同`
   - 公共 error/example/status/projection helper。
   - schema/contract/unit tests。

3. `修复：统一信号发现与时钟采样正确性`
   - not-found、clock bracket、sampled pulse、signal changes/stability。
   - 跨 action golden tests。

4. `改进：补齐波形分析证据并精简默认响应`
   - waveform/event/list/cursor/source/rc 输出合同。
   - scan/truncation 和 compact/verbose tests。

5. `改进：统一 APB AXI Stream 配置时间与事务输出`
   - effective config、time、latency、payload projection、stream export schema。
   - APB/AXI/stream contract 与真实 fixture tests。

6. `重构：补齐 xdebug xcov MCP 会话生命周期管理`
   - shared manager 状态机、capability、tombstone、doctor/kill/gc、query guard。
   - direct/fake LSF/real LSF 测试。

7. `文档：同步 action 合同 MCP 工具与 agent skill`
   - README/help/examples/doc/agents/skills。
   - schema/example/skill parity 检查。

8. `测试：收口全 action 回归与全仓库验证门禁`
   - 缺失的回归断言、测试工具和最终审计脚本；不得引入 70-action replay runner。
   - clean 后 `make test`、`make full-test`、nightly/LSF 结果写入 commit body。

若某一阶段只修改已有 commit 中尚未推送的测试或文档，应优先保持上述语义切分，而不是制造大量无意义小 commit；禁止用一个“大杂烩”提交吞并全部改动。

## 11. Push 计划与门禁

目标远端和当前基线：

- 当前分支：`master`
- 远端：`origin`
- push 目标：执行时再次确认当前分支和 upstream，不硬编码假设

push 前必须全部满足：

1. 第 9 节 required tests 全部 PASS。
2. required 真实环境测试没有被 fake、schema-only 或低层级 smoke 替代。
3. `git diff --check` 通过。
4. `git status --short` 仅包含明确允许保留的本地文件；正常交付应为 clean。
5. 检查最近 commits 的中文 subject/body、验证记录和文件范围。
6. 确认未提交 token、cookie、license 内容、完整唯一 ID、绝对私有资源路径和大型测试产物。
7. 获取当前 `HEAD`、upstream 和 ahead/behind 状态。

执行：

```bash
git push origin <当前目标分支>
```

push 后回报：

- 推送分支。
- commit 范围和最终 commit ID。
- 远端结果。
- clean/full test、nightly、real LSF 的 PASS/FAIL/NOT RUN 摘要。
- 任何 external fixture SKIP 及其原因。

如果 push 因网络、认证、权限或远端分支变化失败，不改变 remote、不改 transport、不强推；保留本地 commits，报告准确阻塞并等待用户决定。

## 12. Goal 模式执行检查表

后续进入 goal 模式后，每一阶段必须更新以下状态：

- [x] 阶段 0：基线与 70 action 台账完成
- [ ] 阶段 1：公共 error/status/projection 合同完成
- [ ] 阶段 2：P0 正确性问题完成
- [ ] 阶段 3：waveform/list/event/cursor 输出完成
- [ ] 阶段 4：APB/AXI/stream 合同完成
- [ ] 阶段 5：xdebug/xcov MCP 生命周期完成
- [ ] 阶段 6：schema/docs/examples/skills 同步完成
- [ ] 70 action 逐项验收全部完成
- [ ] `make clean` 完成
- [ ] `make test` PASS
- [ ] `make full-test` PASS
- [ ] `make -C xdebug test-nightly` PASS
- [ ] real LSF PASS 或按规则明确 NOT RUN/阻塞
- [ ] 所有阶段 commits 完成且范围正确
- [ ] 最终工作树检查完成
- [ ] 推送远端完成

## 13. 计划变更规则

- 小型实现细节由执行 agent 直接决定，但必须遵守公共合同和 helper 优先原则。
- 会改变 public schema、not-found 语义、session ownership、清理权限、compact 默认字段或 required test/push gate 的问题属于关键决策，必须与用户重新对齐并更新本文件。
- 新发现问题优先归入现有公共语义族；只有无法由现有 helper 表达时才增加新抽象。
- 如果同一阻塞连续三轮没有进展，按 goal 规则启动独立子 agent 求证；正式完成前仍需主 agent 评审结论。
- 不允许为了赶进度删除失败测试、降低断言、跳过 required test 或引入 fallback。
