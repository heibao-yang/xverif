# xdebug Action 返回可用性重新评审报告（2026-07-10）

## 背景

本报告替代继续直接执行旧修复计划。原因是 2026-07-09 之后已经完成一轮共性修复，并在 2026-07-10 删除了 xdebug MCP raw 请求入口，旧评审文档中的部分问题已经不再成立，必须基于当前代码和真实 replay evidence 重新确认。

旧评审来源：

- `doc/xdebug_mcp_action_return_review_2026-07-09.md`

当前基线：

- `c568c0d 修复：收紧波形表达式 action 请求合同`
- `9ef2303 工具：新增 xdebug action 返回 replay harness`
- `c8f5275 修复：打通 xdebug action native 全量 replay`
- `7dced5e 工具：补齐 xdebug MCP 与负例 replay 闭环`
- `6e41333 工具：补齐 xdebug MCP L3 错误 replay`
- `5d21e55 删除：移除 xdebug MCP raw 请求入口`

## 重新评审范围

本轮只使用当前代码的真实 replay 结果做判断：

- L0 静态合同：70 个 action registry、runtime catalog、schema、example。
- L2 成功路径：native CLI JSON/xout、MCP direct JSON/xout。
- L3 错误路径：native CLI JSON/xout、MCP direct JSON/xout。

不再评审 `xverif_debug_raw_request`，因为该 xdebug MCP public API 已删除。完整 native envelope 入口仍属于 CLI/SDK-free 路径，不属于 MCP action 可用性问题。

## 当前 Replay Evidence

| 层级 | 入口 | 结果 | evidence |
|---|---|---:|---|
| L0 | static | 70/70 通过 | `/tmp/xdebug_action_return_replay_20260710_004832` |
| L2 | native JSON/xout | 140/140 通过 | `/tmp/xdebug_action_return_replay_20260710_004847` |
| L2 | MCP JSON/xout | 132/132 通过 | `/tmp/xdebug_action_return_replay_20260710_005015` |
| L3 | native JSON/xout | 274/274 通过 | `/tmp/xdebug_action_return_replay_20260710_005129` |
| L3 | MCP JSON/xout | 244/244 通过 | `/tmp/xdebug_action_return_replay_20260710_005159` |

执行命令：

```bash
python xdebug/tools/replay_action_returns.py --layer L0 --write-matrix /tmp/xdebug_action_return_review_current_matrix.md
python xdebug/tools/replay_action_returns.py --layer L2 --entry native-all --timeout-sec 180
python xdebug/tools/replay_action_returns.py --layer L2 --entry mcp-all --timeout-sec 180
python xdebug/tools/replay_action_returns.py --layer L3 --entry native-all --timeout-sec 120
python xdebug/tools/replay_action_returns.py --layer L3 --entry mcp-all --timeout-sec 120
```

L2/L3 涉及真实 FSDB、daidir、NPI 和 MCP direct session，按仓库规则在沙箱外执行。

## 已确认不再成立的问题

### 1. MCP raw wrapper 削弱结构化错误

旧报告大量问题来自 `xverif_debug_raw_request`：

- backend error 被包装成 `XVERIF_CLI_FAILED`。
- `invalid_arg`、`expected`、`correct_example` 只能从 `stdout_tail` 解析。
- query/raw/native 示例混杂，AI 容易把 native envelope 当 MCP tool 参数。

当前状态：已由 `5d21e55` 删除 xdebug MCP raw request public API。该问题按 API removal 关闭，不再进入 action 修复计划。

### 2. 70 action 基础成功返回可执行性

当前 L2 replay 证明：

- 70 个 action 的 native JSON/xout 成功路径全部可执行。
- 66 个 MCP 可合理调用入口的 JSON/xout 成功路径全部可执行。
- 成功返回至少满足 runner 当前检查：`ok:true`、action 对齐、xout 可识别、MCP tool 返回形态可消费。

因此旧报告中“只因没有当前成功证据而需要重跑确认”的项，当前已被 replay 覆盖。

### 3. 表达式类 schema 缝隙的基础回归

旧报告重点提到：

- `value.batch_at` schema/runtime 冲突。
- `verify.conditions` 弱约束导致假阳性。
- `window.verify` 失败样本和条件约束不足。

当前状态：

- `c568c0d` 已收紧相关请求合同。
- L0、L2、L3 replay 当前全部通过。

结论：这些 action 的基础 schema/成功/错误 replay 已通过；是否还需要更强语义样本，应归入下一轮 domain-specific case，而不是继续按旧共性问题处理。

### 4. session lifecycle 入口混淆

当前状态：

- xdebug MCP raw request 已删除。
- MCP session lifecycle 通过专用 tool 覆盖。
- `xverif_debug_query` 调 native `session.*` 的 guard 已保留测试。

结论：旧报告中 raw/native session lifecycle 混杂问题不再作为 action 返回可用性修复项。

## 不能据此关闭的问题

当前 replay 全绿不等于旧报告所有可用性问题均已修复。原因是 L3 `handler_error` 覆盖面仍偏 wrapper/session：

- native L3 多数 handler evidence 使用 `target.session_id:"no_such_session"`。
- MCP L3 多数 handler evidence 使用 `session_id:"no_such_session"`。
- 这能证明 session/wrapper 错误返回，但不能证明 action-specific domain 错误。

因此，下面旧问题必须重新补 domain replay 后再判断是否已修复：

| 问题族 | 旧报告典型 action | 当前结论 |
|---|---|---|
| `config_path` 不存在缺 `invalid_arg/expected/correct_example` | `apb.config.load`、`axi.config.load`、`event.config.load`、`stream.config.load`、`rc.generate` | 未由当前 L3 证明关闭 |
| config/name not found 回显坏 name，缺 available configs | `apb.query`、`axi.query`、`axi.analysis`、protocol cursor/query 类 | 未由当前 L3 证明关闭 |
| signal/clock path not found 回显坏路径，缺 `scope.roots/scope.list` next action | `value.at`、`value.batch_at`、`signal.*`、`counter.statistics`、`trace.active_driver*` | 未由当前 L3 证明关闭 |
| 0 path/empty result 缺 `empty_reason/status` | `trace.driver`、`trace.load`、`scope.list` | 未由当前 L3 证明关闭 |
| partial success 计数和 missing reason 不足 | `value.batch_at`、`list.value_at`、export/query 类 | L2 成功通过，但未做强语义断言 |
| output path/dir/file 字段命名不一致 | `list.export`、`stream.export`、`rc.generate`、`axi.export` | 未由当前 replay 判定关闭 |
| xout 噪声、路径、底层 session metadata 过多 | `session.list`、`session.doctor`、`session.gc` | 未由当前 replay 判定关闭 |
| action 名称与直觉冲突导致误用 | `event.export` 不接受 `output.path` | 未由当前 replay 判定关闭 |

## 新的修复计划目标

后续目标不再是直接按旧报告修所有条目，而是先升级 review/replay：

1. 在 `xdebug/testdata/action_return_replay/cases.json` 中为旧报告问题族补 `domain_error` / `semantic_empty` / `partial_success` case。
2. 扩展 `xdebug/tools/replay_action_returns.py`，让 L3 不再只用 `no_such_session` 代表 handler error，而是优先执行 registry 中的 action-specific domain case。
3. 对每个旧报告问题族输出当前证据状态：`confirmed_fixed`、`confirmed_bug`、`api_removed`、`not_reproduced`、`coverage_gap`。
4. 只把 `confirmed_bug` 放入后续修复 goal；`api_removed` 和 `confirmed_fixed` 不再修。
5. 每个 `confirmed_bug` 必须保留 before/after evidence，并补防回归测试。

## 本轮结论

- 当前 70-action replay 基础矩阵全绿，没有发现新的 replay 失败。
- xdebug MCP raw request 相关旧问题已通过 API 删除关闭。
- 旧报告中的 action-specific domain 错误不能直接按“已修复”处理，因为当前 L3 handler evidence 多数覆盖的是 session not found，不是旧报告里的真实 domain 错误。
- 下一步应先补 domain replay case，再基于当前真实 evidence 生成最终缺陷清单。
