# xdebug Action 返回全量 Replay 修复报告（2026-07-09）

## 结论

- L0 静态合同：70/70 通过。
- Native L2 成功路径：70 action x JSON/xout 共 140/140 通过。
- MCP direct L2 成功路径：66 个 MCP 可调用入口 x JSON/xout 共 132/132 通过。
- Native L3 负例路径：schema/handler error 共 274/274 通过。
- MCP direct L3 负例路径：query action schema/handler error 共 244/244 通过。
- Runtime 真缺陷修复：`axi.analysis` 空统计结果不再返回 `ACTION_FAILED`，改为 `ok:true`、`samples:0`、`status:empty`。

`actions`、`session.list`、`session.gc` 没有独立 handler/domain error 面：它们的 target/session_id 不参与 handler 失败判定，因此 L3 只保留 schema_error replay。

## Evidence

Before:

- `/tmp/xdebug_action_return_replay_20260709_233937`：初始 native L2，140 项中 100 项失败。
- `/tmp/xdebug_action_return_replay_20260709_234510`：fixture/setup 修正后 native L2 剩 16 项失败。
- `/tmp/xdebug_action_return_replay_20260709_234749`：定位 `axi.analysis` native JSON/xout 2 项失败。

After:

- `/tmp/xdebug_action_return_replay_20260709_233825`：L0 静态合同 70/70 通过。
- `/tmp/xdebug_action_return_replay_20260709_235032`：native L2 140/140 通过。
- `/tmp/xdebug_action_return_replay_20260709_235659`：MCP direct L2 132/132 通过。
- `/tmp/xdebug_action_return_replay_20260710_000004`：native L3 274/274 通过。
- `/tmp/xdebug_action_return_replay_20260710_000451`：MCP direct L3 244/244 通过。

## 修复内容

1. 新增 `xdebug/tools/replay_action_returns.py`。
   - 支持 L0 静态合同、native JSON/xout、MCP direct JSON/xout、native L3 schema/handler error、MCP direct L3 query error。
   - 每个 case 写入 `/tmp/xdebug_action_return_replay_<timestamp>/evidence/<case_id>/request.json`、`response.json` 或 `response.xout`、`metadata.json`。
   - metadata 记录 command、entry、output_format、elapsed_ms、returncode、fixture 和 setup steps。

2. 新增 `xdebug/testdata/action_return_replay/cases.json`。
   - 覆盖 runtime catalog 当前 70 个 implemented action。
   - 绑定 existing basic request examples、fixture profile、MCP 可调用性和专用 MCP tool。

3. 新增 `doc/xdebug_action_return_replay_matrix_2026-07-09.md`。
   - 人读矩阵列出 70 个 action 的 native/MCP JSON/xout 覆盖状态。

4. 新增 `xdebug/tests/contract/test_action_return_replay_registry.py`。
   - 防止 registry 漏 action、example、request schema 或 response schema。
   - 防止矩阵生成漏列 action。

5. 修复 `xdebug/src/engine/service/actions/protocol/axi_analysis.cpp`。
   - `axi.analysis` 在 config 存在、analyze 成功但统计样本为空时返回成功空结果。
   - latency/osd 均返回 `samples:0`、`status:empty`，避免把可解释空结果伪装成 handler failure。

## 验证命令

```bash
make -C xdebug all
python -m py_compile xdebug/tools/replay_action_returns.py
python -m pytest xdebug/tests/contract/test_action_return_replay_registry.py -q
python xdebug/tools/replay_action_returns.py --layer L0 --write-matrix
python xdebug/tools/replay_action_returns.py --layer L2 --entry native-all --timeout-sec 180
python xdebug/tools/replay_action_returns.py --layer L2 --entry mcp-all --timeout-sec 180
python xdebug/tools/replay_action_returns.py --layer L3 --entry native-all --timeout-sec 120
python xdebug/tools/replay_action_returns.py --layer L3 --entry mcp-all --timeout-sec 120
```

涉及 FSDB/daidir/NPI/MCP direct 的 replay 均在沙箱外执行。
