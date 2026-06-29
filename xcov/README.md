# xcov

`xcov` 是面向 AI/MCP 的 VCS/Verdi coverage database 查询工具。它读取
`simv.vdb`、`merged.vdb`，接受 `xcov.v1` JSON 请求，默认返回紧凑的
`xout`；需要机器 JSON 时设置 `output.response_format:"json"`，或使用
`tools/xcov --json -`。

xcov 只以 VDB/Python NPI coverage API 为数据源，不解析 URG HTML、
`asserts.html`、`mod*.html` 或 `session.xml`。只有 Verdi/pynpi 文档、headers
和真实 VDB probe 证实可获取的字段才会进入公开 schema；拿不到的 URG 字段不
提供接口，也不会用 note 占位。

## 快速开始

一次性 JSON 查询：

```bash
printf '%s\n' '{"api_version":"xcov.v1","action":"session.open","target":{"vdb":"fake"},"args":{"name":"cov0","fake":true}}' \
  | tools/xcov --json -
```

MCP/长会话使用 stdio-loop：

```bash
tools/xcov --stdio-loop
```

stdio-loop 启动后输出 `protocol:"xcov-stdio-loop"` ready 行，后续每行接收一个
JSON request，并返回包含 `xout` 和 `json` payload 的 JSONL envelope。NPI 诊
断输出会导向 stderr，stdout 保持机器可解析。

## 真实 NPI 运行

真实 VDB 查询需要 Synopsys Verdi/Python NPI 和 license。按项目规则，NPI、
VCS、VIP、真实 coverage probe 必须在沙箱外运行。

已验证的本地形态：

```text
Python 3.11
VERDI_HOME=/home/yian/Synopsys/verdi/V-2023.12-SP2
示例 VDB=/home/yian/uart_example/sim/merged.vdb
```

## MCP 工具

`xverif_mcp` 暴露 xcov stateful backend：

```text
xverif_cov_session_open
xverif_cov_session_list
xverif_cov_session_use
xverif_cov_session_close
xverif_cov_query
xverif_cov_raw_request
xverif_cov_list_actions
xverif_cov_get_schema
```

环境变量：

- `XVERIF_MCP_ENABLE_COV=0`：隐藏 coverage 工具。
- `XVERIF_XCOV_BIN`：覆盖 xcov 可执行文件。
- `XVERIF_XCOV_PYTHON`：覆盖 xcov Python runtime。
- `XVERIF_XCOV_VERDI_HOME`：覆盖 `VERDI_HOME`。
- `XVERIF_XCOV_LOG_DIR`：覆盖日志目录。
- `XVERIF_XCOV_LOG=0`：关闭日志。

## 常用请求

打开 session：

```json
{"api_version":"xcov.v1","action":"session.open","target":{"vdb":"merged.vdb"},"args":{"name":"cov0"}}
```

查询 holes：

```json
{"api_version":"xcov.v1","action":"code_coverage.holes","target":{"session_id":"cov0"},"args":{"scope":"uart_tb","metrics":["line","toggle","branch","condition","fsm","assert"],"limits":{"max_items":100}}}
```

源码位置反查 coverage object：

```json
{"api_version":"xcov.v1","action":"source.map","target":{"session_id":"cov0"},"args":{"file":"rtl/ctrl.sv","line":123,"window":3}}
```

源码窗口加 coverage annotation：

```json
{"api_version":"xcov.v1","action":"source.annotate","target":{"session_id":"cov0"},"args":{"file":"rtl/ctrl.sv","line":123,"window":3}}
```

assert 结构化报告：

```json
{"api_version":"xcov.v1","action":"assert.report","target":{"session_id":"cov0"},"args":{"include_source":true}}
```

导出 code coverage 未达标项：

```json
{"api_version":"xcov.v1","action":"export.code_coverage","target":{"session_id":"cov0"},"args":{"scope":"uart_tb","threshold_pct":100.0,"output":{"path":"code_coverage.md"}}}
```

导出 functional coverage 未达标 bin：

```json
{"api_version":"xcov.v1","action":"export.function_coverage","target":{"session_id":"cov0"},"args":{"covergroup":"*","threshold_pct":100.0,"output":{"path":"function_coverage.md"}}}
```

导出 assertion coverage：

```json
{"api_version":"xcov.v1","action":"export.assert","target":{"session_id":"cov0"},"args":{"scope":"uart_tb","threshold_pct":100.0,"output":{"path":"assert.md"}}}
```

## URG 对齐语义

`code_coverage.summary`、`code_coverage.holes`、`metrics.list`、`scope.summary`、
`source.map` 和 Markdown exports 使用与 URG HTML 报告一致的 score-bearing
object 层级：

- Line：`npiCovStmtBin`
- Condition：`npiCovConditionBin`
- Toggle：`npiCovToggleBin`
- Branch：`npiCovBranchBin`
- FSM：`npiCovTransBin`
- Assert：`npiCovAssert`、`npiCovCoverProperty`、`npiCovCoverSequence`

中间对象，例如 line block、toggle signal、branch/condition object、FSM state
container、assert Attempt/Success/Failure bin，会被遍历用于上下文和证据，但不
计入公开 summary 分母。

`functional.summary` 的 covergroup score 按 URG group 页语义计算：优先取直接
coverpoint/cross coverage 百分比的平均值；原始 covergroup 计数保留在
`raw_covered/raw_coverable/raw_coverage_pct`。

## Action 合同

- `scope.summary` / `scope.children`：返回当前层次或子层次的扁平覆盖率字段，例如
  `coverage_pct`、`line_pct`、`toggle_pct`、`branch_pct`、`condition_pct`、
  `fsm_pct`、`assert_pct`、`functional_pct`，并带 module 对应的 `file/line`
  evidence（若 NPI 暴露）。
- `scope.search`：只返回 `name/full_name`，用于定位 hierarchy 名称。
- `code_coverage.summary`：按 metric、scope 或 source file 汇总代码覆盖率。
- `code_coverage.holes`：按输入 hierarchy 输出当前层次和子模块的覆盖率概览，不展开
  具体 signal、branch、condition 或 bin。具体未覆盖项请使用
  `export.code_coverage`。
- `source.map`：按源码 file/line/window 反查 coverage item。
- `source.annotate`：基于 NPI `file_name()/line_no()` evidence 和项目源码文件输出源码
  窗口。它可以挂接 line/branch/condition/toggle/assert object annotation，但不承诺
  URG HTML 专有展示标签，例如 `MISSING_ELSE`；除非后续 NPI API probe 证明这些标签
  可取。
- `assert.report`：输出 assert/cover property/cover sequence 的 category/severity
  summary，以及 attempts、real successes、failures、incomplete、first match。

## Markdown 导出

`export.code_coverage`、`export.function_coverage`、`export.assert` 只输出 Markdown
文件，不保留 `output.artifact_format` 选择。响应的 `summary.note` 会提示：需要复杂
处理、二次统计或跨报告加工时，请调用 `x-npi` 学习 `pynpi` coverage API 并编写脚本。

导出路径使用 `args.output.path`：

- 相对路径写到 `.xverif/xcov_exports/`。
- 包含 `..` 的路径会被拒绝。
- 绝对路径必须显式设置 `output.allow_absolute_path=true`。

## 日志

xcov 日志默认写入：

```text
~/.xverif/xcov/sessions/<session_id>/session.json
~/.xverif/xcov/sessions/<session_id>/logs/actions.ndjson
~/.xverif/xcov/backend/sessions/<session_id>/logs/lifecycle.ndjson
~/.xverif/xcov/backend/sessions/<session_id>/logs/transport.ndjson
```

日志事件包含 `ts/event_id/pid/layer/component/session_id/action/phase/ok/context`，
不会记录完整大型 `items` payload。

## 审阅材料

- API 能力审计：[docs/coverage-api-capability.md](docs/coverage-api-capability.md)
- 所有 action 的 `xout` 样例：[docs/action-xout-examples.md](docs/action-xout-examples.md)

## 当前限制

- `test="each"` 尚未实现；使用 `test="merged"` 或具体 test name。
- `source.annotate` 不解析 URG HTML，不输出未证实的 URG 专有源码标注。
