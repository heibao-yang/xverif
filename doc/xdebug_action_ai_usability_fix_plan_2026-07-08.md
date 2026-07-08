# xdebug action 分类优化与 AI 可用性修复计划

## Summary

基于 `doc/xdebug_action_entry_review_2026-07-08.md` 和 fresh-agent 报告，修复 xdebug action 的入口合同、schema/runtime 漂移、MCP/native 输出差异，以及子 agent 调用中暴露出的参数误用问题。

实施第一步必须先把本计划写入 repo 内 `doc/`，然后进入 goal 模式，以该计划书内容作为 goal 开始任务。失败重试记录不删除；它作为 AI 可用性证据进入修复范围。目标是让 AI 通过 action catalog、schema、错误提示和 skill 示例就能更稳定地构造正确请求。

## Key Changes

- 任务启动流程：
  - 先新增本计划文档，完整记录计划、决策、测试矩阵和验收标准。
  - 计划文档落盘后，创建 goal，以该计划书内容为任务目标开始实施。
  - 后续实施、验证、报告更新都必须对照该计划书推进；如中途变更决策，先更新计划文档再继续。

- 修复硬合同漂移：
  - `list.export` 统一 request schema、runtime accepted format、错误提示和 response format，公开输入只保留确认支持的 `u64bin`。
  - `list.delete` 修复 schema 允许 integer index 但 runtime 抛类型异常的问题，支持合法 integer/string index，并返回稳定错误码。
  - `stream.export.kind` 收紧为 `transfer | packet | packet_beats`，明确拒绝 `beats`，错误提示给出正确枚举。
  - MCP `raw_request(output_format=xout)` 不再把 backend 成功 XOUT 当 JSON parse 错误；JSON 返回 JSON，XOUT 返回文本，完整 envelope 仍用于 debug。

- 修复 AI 参数易错点：
  - 将报告中的失败重试按原因分类：字段位置错误、同名字段跨 action 混用、enum 猜错、envelope 层级混淆、session/config 状态污染、timeout/窗口过大。
  - 对高风险 action 增加 schema 级拒绝、runtime 错误 hint、skill 最小正确模板和常见错误反例。
  - 重点覆盖 `apb.query`、`axi.query`、`axi.request_response_pair`、`stream.export`、`trace.active_driver_chain`、`batch`、`session.*`、`event.*`、`value.batch_at`。
  - 错误 hint 要直接指出正确字段位置，例如 `limit 应写在 query.limit`、`depth 应写在 limits.max_depth`、`direction 不适用于该 action`。

- 统一旧 action 的表达式查询合同：
  - 直接修改旧 `event/window/signal` action，不新增并行新 action。
  - 标准字段统一为 `expr`、`clock`、`signals`、`time_range.begin/end`。
  - 删除对旧误用字段的公开支持；不做 alias，不做 deprecated warning。

- 重构 APB/AXI 公共协议参数：
  - 查询类统一使用 `query.limit` / `query.index`，替代旧 `num` 等分散字段。
  - 不保留旧字段兼容；旧字段 schema 直接拒绝。
  - 对不同 AXI action 明确 `direction` 是否适用，避免 AI 从相邻 action 复制字段。

- 收紧 response 合同：
  - 70 个 action 第一批全部补强 compact/default response schema。
  - compact/default 输出保持严格、稳定、适合 AI 读取。
  - full/debug 输出允许保留扩展诊断字段。
  - 每个高风险 action 的 response 至少保留调试关键字段：time、signal/path、value、name、config、count、truncated、cause 或 source context。

- 更新文档与 skill：
  - 更新 `skills/xverif` 中 action 示例，默认使用 xout，不写 `output_format:"json"`，除非专门展示 JSON。
  - 增加高风险 action cookbook：最小正确请求、常见错误请求、错误原因、正确 response 关键字段。
  - 更新 `doc/agents/xdebug` 的 action 开发、schema 校验、MCP/native envelope、session/config 生命周期说明。
  - 在主评审报告中保留 retry evidence，并新增“AI 参数误用修复映射”小节，说明每类错误如何被 schema/docs/runtime 修复。

## Test Plan

- 启动与 goal 验收：
  - `test -f doc/xdebug_action_ai_usability_fix_plan_2026-07-08.md`
  - 确认 goal 内容来自该计划书，并在最终交付中说明 goal 是否完成。

- Schema 与 contract：
  - `make -C xdebug schema-test`
  - `make -C xdebug contract-test`
  - 增加负例：`args.limit`、旧 `num`、`kind=beats`、`args.depth`、非法 `direction`、错误 envelope 层级。
  - 增加正例：`query.limit/query.index`、`limits.max_depth`、`stream.export.kind=packet_beats`、integer/string `list.delete.index`。

- Runtime focused tests：
  - native `tools/xdebug --json -` 覆盖所有修复 action。
  - MCP `xverif_debug_query` 覆盖默认 xout。
  - MCP `xverif_debug_raw_request` 覆盖 JSON、XOUT、debug envelope 三种输出合同。
  - AXI/APB 大窗口 timeout 返回明确 transport/timeout 错误，不伪装成 schema 或 action 失败。

- Docs/examples validation：
  - 校验 `skills/xverif` 中所有 xdebug JSON 示例。
  - 检查示例不再出现旧字段：`args.limit`、旧 `num`、`kind=beats`、`args.depth`、错误 `direction`。
  - 检查高风险 cookbook 中每个 action 都有正确模板和错误反例。

- Evidence rebuild：
  - 重新生成 70 action native + MCP evidence。
  - 重新生成 `doc/xdebug_action_entry_review_2026-07-08.md`。
  - 再启动 fresh-context 子 agent，用新的 req/rsp evidence 独立评审。
  - 对比旧报告，确认参数误用类 retry 明显减少；若仍存在，逐项记录为后续 action 设计问题。

- 环境与清理：
  - 涉及 NPI、VCS、VIP、license、真实 FSDB、MCP stdio/UDS 的测试按仓库规则在沙箱外运行。
  - 每轮测试后检查并清理本轮 xdebug session 和 engine 进程残留。

## Assumptions

- 允许破坏旧误用请求兼容；不保留 alias，不做 deprecated warning。
- 子 agent 的错误参数不是人工失误噪声，而是 action 入口可用性问题，必须转化为 schema、错误提示、模板和文档改进。
- 本轮先写计划文档，再进入 goal 模式；计划文档是后续实施的 source of truth。
- 不隐藏失败重试；报告继续保留 first attempt、final attempt、known bad template、final good template。
- 不修改 `session.gc` 行为，只明确它有清理副作用。
- 不新增平行 action；统一工作直接落在现有旧 action 上。
