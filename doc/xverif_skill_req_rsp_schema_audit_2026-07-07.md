# xverif skill req/rsp schema 一致性审计报告

日期：2026-07-07

## 范围与方法

本轮按用户要求启动 3 个子 agent 并行检查，再由主 agent 交叉复核。检查对象是当前工作树中的 `skills/xverif/**/*.md`，对照当前仓库内真实 schema 与少量 runtime 约束：

- request schema：`xdebug/schemas/v1/actions/*.request.schema.json`
- response schema：`xdebug/schemas/v1/actions/*.response.schema.json`
- xcov schema：`xcov/xcov/schemas.py`
- stream runtime 约束：`xdebug/src/engine/service/actions/stream/*`、`xdebug/src/waveform/stream/*`

注意：开始检查时 `skills/xverif/...` 已有未提交改动；本报告基于当前本地工作树，不回退或覆盖这些改动。

## 自动化与交叉检查摘要

- 已对 skill 文档中的 xdebug / xcov JSON 片段做抽取检查；MCP query 片段因不是完整 native envelope，需要按 MCP 参数形态单独判断。
- request 侧发现多个可以被 schema 直接证明的硬不一致。
- response 侧发现 1 个字段硬冲突，以及若干 coverage / 显式性问题。
- xcov 示例未发现当前 schema 级不一致。

## 硬不一致：request 描述或示例与 schema 冲突

### 1. `value.batch_at` 示例缺少必填 `clock`

位置：

- `skills/xverif/references/xdebug/examples.md:34`
- `skills/xverif/references/xdebug/examples.md:506`

文档现状：

- 两个 `value.batch_at` 示例都在 `args` 中给了 `time`、`signals`，但没有 `clock`。
- 第二个示例还把 `slice_hint` 放进 `value.batch_at.args`，见下一条。

schema 事实：

- `xdebug/schemas/v1/actions/value.batch_at.request.schema.json:102` 要求 `args.required = ["signals", "time", "clock"]`。
- `xdebug/schemas/v1/actions/value.batch_at.request.schema.json:101` 设置 `args.additionalProperties:false`。

影响：

- 用户按示例调用会被 request schema 拒绝。

建议：

- 所有 `value.batch_at` 示例补 `args.clock`。
- 第二个示例如果需要切片提示，应改成 `value.at`，或先扩展 `value.batch_at` schema/runtime。

### 2. `value.batch_at` MCP 示例把非法 `limit` 放进 `args`

位置：

- `skills/xverif/references/xdebug/json-api.md:21`
- `skills/xverif/references/xdebug/json-api.md:31`

文档现状：

- MCP query 示例中 `action:"value.batch_at"`，`args` 内包含 `"limit": 100`。

schema 事实：

- `value.batch_at.args` 只允许 `clock`、`edge`、`format`、`sample_point`、`signals`、`time`、`time_unit`。
- `xdebug/schemas/v1/actions/value.batch_at.request.schema.json:101` 禁止额外字段。

影响：

- 该片段作为 action 参数传入会失败。

建议：

- 删除 `args.limit`。
- 如果需要限制输出，改用 action 支持的顶层 `limits`，或换成真实支持 `args.limit` 的 action 示例。

### 3. 文档暗示 `value.batch_at` 支持 `slice_hint`

位置：

- `skills/xverif/references/xdebug/json-api.md:300`
- `skills/xverif/references/xdebug/json-api.md:303`
- `skills/xverif/references/xdebug/examples.md:517`

文档现状：

- `json-api.md` 写“需要 xbit 切字段时给 `value.at` 或 `value.batch_at` 加 `slice_hint`”。
- `examples.md` 的 `value.batch_at` 示例直接包含 `slice_hint`。

schema 事实：

- `xdebug/schemas/v1/actions/value.at.request.schema.json:71` 有 `slice_hint`。
- `xdebug/schemas/v1/actions/value.batch_at.request.schema.json` 没有 `slice_hint`，并且 `args.additionalProperties:false`。

影响：

- 用户给 `value.batch_at` 加 `slice_hint` 会被 schema 拒绝。

建议：

- 文档改成只有 `value.at` 支持 `slice_hint`。
- 如果希望 batch 支持，需要先改 schema、runtime 和测试。

### 4. `handshake.inspect` 示例使用非法 `max_stall_cycles`

位置：

- `skills/xverif/references/xdebug/examples.md:107`
- `skills/xverif/references/xdebug/examples.md:116`

文档现状：

- 示例在 `handshake.inspect.args` 下直接写 `"max_stall_cycles": 16`。

schema 事实：

- `xdebug/schemas/v1/actions/handshake.inspect.request.schema.json:101` 禁止额外字段。
- 当前 `args` 允许字段包括 `clock`、`data`、`edge`、`limit`、`ready`、`rules`、`sample_point`、`time_range`、`time_unit`、`valid`。

影响：

- 示例 request 会被 schema 拒绝。

建议：

- 删除 `max_stall_cycles`，或改写为当前 schema 允许的 `rules` 结构。
- 如果 runtime 需要一等支持该字段，应补 schema、实现和测试。

### 5. `apb.config.load` / `axi.config.load` args contract 少写 `config/config_path` 约束

位置：

- `skills/xverif/references/xdebug/action-reference.md:58`
- `skills/xverif/references/xdebug/action-reference.md:66`

文档现状：

- 表格只写 `required: name`。

schema 事实：

- `apb.config.load` schema 要求 `name`，并且 `config` / `config_path` 二选一。
- `axi.config.load` schema 要求 `name`，并且 `config` / `config_path` 二选一。
- 两个 schema 的 `x-arg_contract_notes` 均为 `required: name; also one of: config / config_path`。

影响：

- 文档低估必填条件；用户只传 `name` 会失败。

建议：

- 表格改成 `required: name; also one of: config / config_path`。

### 6. `stream.config.load` args contract 比 schema 更窄

位置：

- `skills/xverif/references/xdebug/action-reference.md:104`

文档现状：

- 表格写 `required: streams`。

schema 事实：

- `xdebug/schemas/v1/actions/stream.config.load.request.schema.json:63` 要求 `streams` / `config` / `config_path` / `file` 四选一。
- `xdebug/schemas/v1/actions/stream.config.load.request.schema.json:104` 的 `x-arg_contract_notes` 是 `also one of: streams / config / config_path / file`。

影响：

- 文档误导用户以为 `config_path`、`file`、`config` 不是合法输入，也与配置复用 workflow 冲突。

建议：

- 表格改成 `also one of: streams / config / config_path / file`。

### 7. `event.config.load` 描述容易误导用户把配置内容写进 request args

位置：

- `skills/xverif/references/xdebug/action-reference.md:80`

文档现状：

- how-it-works 写“将 name/clock/edge/sample_point/signals/reset 等配置写入 EventManager”。

schema 事实：

- `xdebug/schemas/v1/actions/event.config.load.request.schema.json:54` 限定 `args` 只允许 `config_path`、`name`、`time_unit`。
- `xdebug/schemas/v1/actions/event.config.load.request.schema.json:55` 要求 `name`。

影响：

- 这不是表格 required 字段硬错，但会诱导用户把 `clock/edge/signals/reset` 直接写到 request args；按 schema 会失败。

建议：

- 改成“request args 只传 `name` 和可选 `config_path/time_unit`；clock/edge/signals/reset 来自配置文件内容或已加载配置”。

## 硬不一致：response 字段与 schema 冲突

### 8. `trace.driver` / `trace.load` 的 `data.paths[]` 文档包含 schema 不允许的 `source`

位置：

- `skills/xverif/references/xdebug/response-fields.md:367`
- `skills/xverif/references/xdebug/response-fields.md:380`

文档现状：

- `trace.load` 的 `data.paths[]` item 表中包含 `source` 字段。
- `trace.driver` 的相关段落使用同类 `paths/source_context/signal_path` 结构。

schema 事实：

- `xdebug/schemas/v1/actions/trace.driver.response.schema.json:93` 起的 path item 只允许 `file`、`line`、`source_context`、`signal_path`。
- `xdebug/schemas/v1/actions/trace.driver.response.schema.json:140` 设置 `additionalProperties:false`。
- `trace.load.response.schema.json` 同样约束。

影响：

- 如果 response 带 `data.paths[].source`，schema 校验会失败。
- 如果 runtime 不带该字段，文档会误导 agent 读取不存在的旧字段。

建议：

- 从 `response-fields.md` 删除 `data.paths[].source`，或明确标为非 schema 的历史字段。
- 如果确实要保留 runtime 字段，应同步扩展两个 response schema。

## 覆盖性与弱一致性问题

### 9. `response-fields.md` 覆盖声明不成立

位置：

- `skills/xverif/references/xdebug/response-fields.md:3`
- `skills/xverif/references/xdebug/response-fields.md:1184`

文档现状：

- 文件声称“覆盖当前 action catalog 中的所有命令”。
- “已实现 Action 总表”未列出部分已有 response schema/action，并重复列出 `session.open`。

schema 事实：

- 当前存在但 `response-fields.md` 缺少独立响应字段章节或总表缺项的 action 包括：
  - `axi.export`
  - `counter.statistics`
  - `list.export`
  - `rc.generate`
  - `scope.roots`
  - `stream.config.load`
  - `stream.config.list`
  - `stream.show`
  - `stream.validate`
  - `stream.query`
  - `stream.export`
  - `trace.active_driver_chain`

影响：

- agent 或脚本作者按该文件查 response 字段时会漏掉当前 action，尤其是新强调的 `stream.*`。

建议：

- 用 `xdebug/schemas/v1/actions/*.response.schema.json` 或 runtime action catalog 重新生成总表。
- 补齐缺失 action 的 response 字段章节；如果 schema 当前较宽松，应明确“字段以 runtime/example 为准，schema 只约束 envelope”。

### 10. `rc.generate` 文档字段比 response schema properties 更强

位置：

- `skills/xverif/references/xdebug/response-fields.md:563`
- `skills/xverif/references/xdebug/response-fields.md:564`

文档现状：

- `rc.generate` summary 写了 `missing_signal_count`、`invalid_time_count`。

schema 事实：

- `xdebug/schemas/v1/actions/rc.generate.response.schema.json:32` 到 `:60` 的 summary properties 未列出这两个字段。
- 但该 schema 允许 `summary.additionalProperties:true`，所以不是 schema 校验硬失败。

影响：

- 文档让读者以为这两个字段是稳定受约束字段；schema 没有明确保证。

建议：

- 若字段稳定存在，补进 response schema properties。
- 否则在文档标注为 runtime diagnostic optional 字段。

### 11. `stream.query.match` 示例与 runtime 约束不一致，但 request schema 目前过宽

位置：

- `skills/xverif/references/xdebug/examples.md:179`
- `skills/xverif/references/xdebug/examples.md:180`
- `skills/xverif/references/xdebug/recipes.md:93`
- `skills/xverif/references/xdebug/recipes.md:94`

文档现状：

- 示例写成：

```json
"match": {"opcode": "8'h5a"}
```

runtime 事实：

- 当前 stream query 实现读取 `match.field`、`match.op`、`match.value`、`match.lo`、`match.hi`、`match.mask`。
- `match.field` 缺失时 runtime 会报缺字段错误。

schema 事实：

- `xdebug/schemas/v1/actions/stream.query.request.schema.json:49` 只把 `match` 约束为 object，未约束内部字段。

影响：

- 严格说这不是 schema 校验失败；但按文档调用会触发 runtime 错误。
- schema 过宽导致该类错误无法被 example validation 捕获。

建议：

- 文档示例改为：

```json
"match": {"field": "opcode", "op": "==", "value": "8'h5a"}
```

- 后续可考虑收紧 `stream.query.request.schema.json` 的 `match` object properties。

## 当前未列为问题的项

- MCP query 片段通常省略完整 native envelope；只要文档明确其为 MCP 核心参数片段，缺 `api_version/target` 不单独判为 native schema 错误。
- `action-reference.md` 中 `none` 与 schema `x-arg_contract_notes` 的 `no required args` 属于措辞差异，未判为语义不一致。
- `event.find` / `event.export` 的 `expr + (name 或 clock+signals)` 形态未发现新的 schema 冲突。
- xcov 文档示例未发现当前 schema 级冲突。

## 建议修复顺序

1. 先修会导致请求直接失败的示例：`value.batch_at.clock`、`value.batch_at.limit`、`value.batch_at.slice_hint`、`handshake.inspect.max_stall_cycles`。
2. 修 action-reference 合同表：`apb.config.load`、`axi.config.load`、`stream.config.load`、`event.config.load`。
3. 修 response 硬冲突：删除或重定义 `trace.driver/load` 的 `data.paths[].source`。
4. 补齐 `response-fields.md` 的 action coverage，尤其是 `stream.*`。
5. 决定是否收紧 `stream.query.match` schema，使示例校验能捕获 runtime 必填字段。

