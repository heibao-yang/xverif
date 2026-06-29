# xcov coverage 查询

xcov 查询 VCS/Verdi coverage database（`simv.vdb`、`merged.vdb`）。它负责 coverage evidence，不负责自动解释 hole 根因或生成补测策略。

## 何时使用

- 查询 line/toggle/branch/condition/fsm/assert/functional coverage。
- 用 `scope.*` 和 `code_coverage.*` 按 hierarchy scope 查看覆盖率概览。
- 按源码 file/line/window 反查 coverage item。
- 输出源码窗口和 coverage annotation。
- 输出 assert/cover property/cover sequence 的结构化 report。
- 通过 `export.code_coverage`、`export.function_coverage`、`export.assert` 导出
  Markdown 查看详细未覆盖项。

## 入口

优先 MCP：

- `xverif_cov_session_open`
- `xverif_cov_query`
- `xverif_cov_session_close`
- `xverif_cov_list_actions`
- `xverif_cov_get_schema`
- `xverif_cov_raw_request`

命令行：

```bash
tools/xcov --json -
tools/xcov --stdio-loop
```

真实 NPI coverage 查询需要 Synopsys license；受限沙箱内 license 可能不可达。

## 常用请求

open：

```json
{"api_version":"xcov.v1","action":"session.open","target":{"vdb":"merged.vdb"},"args":{"name":"cov0"}}
```

holes：

```json
{"api_version":"xcov.v1","action":"code_coverage.holes","target":{"session_id":"cov0"},"args":{"scope":"uart_tb","metrics":["line","toggle","branch","condition","fsm","assert"],"limits":{"max_items":100}}}
```

source map：

```json
{"api_version":"xcov.v1","action":"source.map","target":{"session_id":"cov0"},"args":{"file":"rtl/ctrl.sv","line":123,"window":3}}
```

source annotate：

```json
{"api_version":"xcov.v1","action":"source.annotate","target":{"session_id":"cov0"},"args":{"file":"rtl/ctrl.sv","line":123,"window":3}}
```

assert report：

```json
{"api_version":"xcov.v1","action":"assert.report","target":{"session_id":"cov0"},"args":{"include_source":true}}
```

code coverage export：

```json
{"api_version":"xcov.v1","action":"export.code_coverage","target":{"session_id":"cov0"},"args":{"scope":"uart_tb","threshold_pct":100.0,"output":{"path":"code_coverage.md"}}}
```

functional coverage export：

```json
{"api_version":"xcov.v1","action":"export.function_coverage","target":{"session_id":"cov0"},"args":{"covergroup":"*uart*","threshold_pct":100.0,"output":{"path":"function_coverage.md"}}}
```

assert export：

```json
{"api_version":"xcov.v1","action":"export.assert","target":{"session_id":"cov0"},"args":{"scope":"uart_tb","threshold_pct":100.0,"output":{"path":"assert.md"}}}
```

## 读取规则

- 先看 `ok`。
- 看 `summary.matched_count/returned/truncated/output_path/note`。
- coverage item 关注 `metric/type/name/full_name/covered/coverable/missing/status/evidence.file/evidence.line`。
- coverage pct 用 `covered/coverable`，不要用 hit count 代替覆盖率。
- 保留 `excluded/unreachable/illegal` 状态，不要误判为普通 hole。
- 交互查询优先用 `scope.summary`、`scope.children`、`scope.search`、
  `code_coverage.summary`、`code_coverage.holes` 看层次覆盖率。
- `scope.summary`/`scope.children` 返回扁平覆盖率字段；不要期待 `metrics={...}`。
- `scope.search` 每项只返回 `name/full_name`。
- `code_coverage.holes` 只输出当前 hierarchy 与子模块覆盖率概览，不展开具体未覆盖
  signal、branch、condition 或 bin。
- 详细未覆盖项必须用 `export.code_coverage`、`export.function_coverage`、
  `export.assert` 导出 Markdown 查看。
- 三个 export action 只支持 Markdown；复杂二次统计、跨报告处理或自定义格式，转用
  `x-npi` 编写 `pynpi` coverage 脚本。
- `source.annotate` 的源码文本来自项目源文件，coverage annotation 来自 VDB/NPI，不解析 URG HTML。
- `assert.report` 读取 category/severity 和 Attempt/Success/Failure/Incomplete/Firstmatch bin count。
- 找不到 NPI API 支撑的 URG 字段时，不要做 fallback，不要要求 xcov 返回占位字段；应说明该字段做不到。

## 排障

- license/NPI 错误：在沙箱外确认 Verdi/NPI 和 license server。
- action 参数不确定：先用 `xverif_cov_list_actions` 和 `xverif_cov_get_schema`。
- 大结果：设置 limit，必要时 `overflow:"to_file"` 或 output path。
- MCP/LSF/session 问题：读 MCP 或 SDK-free 对应 troubleshooting。
