# xdebug Action Return Replay Matrix（2026-07-09）

本矩阵由 `xdebug/tools/replay_action_returns.py --write-matrix` 生成，覆盖 registry 中的 70 个 action。

| # | action | family | requires | setup | native JSON | native xout | MCP JSON | MCP xout | L0 static |
|---:|---|---|---|---|---|---|---|---|---|
| 1 | `actions` | builtin | none | none | planned | planned | planned | planned | pass |
| 2 | `schema` | builtin | none | none | planned | planned | planned | planned | pass |
| 3 | `batch` | builtin | none | none | planned | planned | n/a | n/a | pass |
| 4 | `session.open` | session | session | combined | planned | planned | planned | planned | pass |
| 5 | `session.list` | session | session | none | planned | planned | planned | planned | pass |
| 6 | `session.doctor` | session | session | combined | planned | planned | n/a | n/a | pass |
| 7 | `session.gc` | session | session | none | planned | planned | n/a | n/a | pass |
| 8 | `session.kill` | session | session | none | planned | planned | n/a | n/a | pass |
| 9 | `session.close` | session | session | combined | planned | planned | planned | planned | pass |
| 10 | `expr.normalize` | design | none | none | planned | planned | planned | planned | pass |
| 11 | `signal.canonicalize` | design | design | design | planned | planned | planned | planned | pass |
| 12 | `signal.resolve` | design | design | design | planned | planned | planned | planned | pass |
| 13 | `source.context` | design | design | design | planned | planned | planned | planned | pass |
| 14 | `trace.driver` | design | design | design | planned | planned | planned | planned | pass |
| 15 | `trace.load` | design | design | design | planned | planned | planned | planned | pass |
| 16 | `trace.active_driver` | combined | combined | combined | planned | planned | planned | planned | pass |
| 17 | `trace.active_driver_chain` | combined | combined | combined | planned | planned | planned | planned | pass |
| 18 | `value.at` | value | waveform | waveform | planned | planned | planned | planned | pass |
| 19 | `value.batch_at` | value | waveform | waveform | planned | planned | planned | planned | pass |
| 20 | `expr.eval_at` | waveform | waveform | waveform | planned | planned | planned | planned | pass |
| 21 | `scope.roots` | scope | waveform | waveform | planned | planned | planned | planned | pass |
| 22 | `scope.list` | scope | waveform | waveform | planned | planned | planned | planned | pass |
| 23 | `signal.changes` | signal | waveform | waveform | planned | planned | planned | planned | pass |
| 24 | `signal.stability` | signal | waveform | waveform | planned | planned | planned | planned | pass |
| 25 | `signal.statistics` | signal | waveform | waveform | planned | planned | planned | planned | pass |
| 26 | `counter.statistics` | counter | waveform | waveform | planned | planned | planned | planned | pass |
| 27 | `detect_abnormal` | waveform | waveform | waveform | planned | planned | planned | planned | pass |
| 28 | `handshake.inspect` | waveform | waveform | waveform | planned | planned | planned | planned | pass |
| 29 | `verify.conditions` | waveform | waveform | waveform | planned | planned | planned | planned | pass |
| 30 | `window.verify` | waveform | waveform | waveform | planned | planned | planned | planned | pass |
| 31 | `rc.generate` | waveform | waveform | waveform | planned | planned | planned | planned | pass |
| 32 | `sampled_pulse.inspect` | waveform | waveform | waveform | planned | planned | planned | planned | pass |
| 33 | `cursor.set` | cursor | waveform | waveform | planned | planned | planned | planned | pass |
| 34 | `cursor.get` | cursor | waveform | waveform | planned | planned | planned | planned | pass |
| 35 | `cursor.list` | cursor | waveform | waveform | planned | planned | planned | planned | pass |
| 36 | `cursor.delete` | cursor | waveform | waveform | planned | planned | planned | planned | pass |
| 37 | `cursor.use` | cursor | waveform | waveform | planned | planned | planned | planned | pass |
| 38 | `list.create` | list | waveform | waveform | planned | planned | planned | planned | pass |
| 39 | `list.add` | list | waveform | waveform | planned | planned | planned | planned | pass |
| 40 | `list.show` | list | waveform | waveform | planned | planned | planned | planned | pass |
| 41 | `list.delete` | list | waveform | waveform | planned | planned | planned | planned | pass |
| 42 | `list.diff` | list | waveform | waveform | planned | planned | planned | planned | pass |
| 43 | `list.validate` | list | waveform | waveform | planned | planned | planned | planned | pass |
| 44 | `list.value_at` | list | waveform | waveform | planned | planned | planned | planned | pass |
| 45 | `list.export` | list | waveform | waveform | planned | planned | planned | planned | pass |
| 46 | `event.config.list` | event | waveform | waveform | planned | planned | planned | planned | pass |
| 47 | `event.config.load` | event | waveform | waveform | planned | planned | planned | planned | pass |
| 48 | `event.find` | event | waveform | waveform | planned | planned | planned | planned | pass |
| 49 | `event.export` | event | waveform | waveform | planned | planned | planned | planned | pass |
| 50 | `stream.config.list` | stream | stream | stream | planned | planned | planned | planned | pass |
| 51 | `stream.config.load` | stream | stream | stream | planned | planned | planned | planned | pass |
| 52 | `stream.query` | stream | stream | stream | planned | planned | planned | planned | pass |
| 53 | `stream.show` | stream | stream | stream | planned | planned | planned | planned | pass |
| 54 | `stream.validate` | stream | stream | stream | planned | planned | planned | planned | pass |
| 55 | `stream.export` | stream | stream | stream | planned | planned | planned | planned | pass |
| 56 | `apb.config.list` | apb | waveform | waveform | planned | planned | planned | planned | pass |
| 57 | `apb.config.load` | apb | waveform | waveform | planned | planned | planned | planned | pass |
| 58 | `apb.cursor` | apb | waveform | waveform | planned | planned | planned | planned | pass |
| 59 | `apb.query` | apb | waveform | waveform | planned | planned | planned | planned | pass |
| 60 | `apb.transfer_window` | apb | waveform | waveform | planned | planned | planned | planned | pass |
| 61 | `axi.config.list` | axi | waveform | waveform | planned | planned | planned | planned | pass |
| 62 | `axi.config.load` | axi | waveform | waveform | planned | planned | planned | planned | pass |
| 63 | `axi.cursor` | axi | waveform | waveform | planned | planned | planned | planned | pass |
| 64 | `axi.query` | axi | waveform | waveform | planned | planned | planned | planned | pass |
| 65 | `axi.analysis` | axi | waveform | waveform | planned | planned | planned | planned | pass |
| 66 | `axi.channel_stall` | axi | waveform | waveform | planned | planned | planned | planned | pass |
| 67 | `axi.latency_outlier` | axi | waveform | waveform | planned | planned | planned | planned | pass |
| 68 | `axi.outstanding_timeline` | axi | waveform | waveform | planned | planned | planned | planned | pass |
| 69 | `axi.request_response_pair` | axi | waveform | waveform | planned | planned | planned | planned | pass |
| 70 | `axi.export` | axi | waveform | waveform | planned | planned | planned | planned | pass |

## 入口说明

- `native JSON`：`tools/xdebug --json -`。
- `native xout`：`tools/xdebug -`。
- `MCP JSON/xout`：通过 direct backend 的 `xverif_debug_query` 或专用 session/schema/list tools。
- `planned` 表示 registry 已纳入矩阵，执行状态以 `/tmp/xdebug_action_return_replay_*/summary.json` 为准。
