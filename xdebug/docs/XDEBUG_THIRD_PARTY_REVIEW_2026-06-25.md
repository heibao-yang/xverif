# xdebug 第三方代码评审报告

日期：2026-06-25

范围：本次评审以 `xdebug` 为主，同时查看了 `xverif_mcp` 中与 xdebug session/MCP 联动相关的实现。评审方式为只读静态审查加多 agent 并行审查，未修改源码，未运行回归测试。

## 总体结论

xdebug 当前的主要风险不是单点功能缺失，而是运行时真源过多、显式 session 合同与少量隐式恢复路径并存、测试层级之间存在断层。显式 session 收紧整体仍然有效：`session.open` 会拒绝 `reuse/reopen`，MCP query 层也要求显式 session；但仍有若干旧兼容路径、fallback 和重复实现会削弱可预期性。

优先级最高的整改方向：

1. 收敛 action 真源：`actions.yaml`、public `ActionRegistry`、engine handler registry、waveform registry/CLI 白名单需要单向生成或强 contract 校验。
2. 收紧隐式 fallback：关闭 session、FSDB 变化、协议 latest config、active trace heuristic 都应把推断和失败显式暴露。
3. 清理旧 waveform 入口：明确 unified engine 是唯一入口，或恢复 standalone waveform target 和测试；不要让旧 router/server 半保留。
4. 补测试断层：MCP smoke 应默认要求 `ok:true`，contract 应增加可执行 example，unit 层应覆盖 APB/AXI/event/stream/list/cursor 的纯逻辑。

## 主要发现

### 1. Action 真源过多，新增/删除 action 容易漂移

严重度：高

证据：

- `xdebug/specs/actions/actions.yaml` 是 checked-in action spec。
- `xdebug/src/api/action_registry_init.cpp:83` 手写 public waveform action 注册。
- `xdebug/src/engine/service/engine_waveform_handlers.cpp:1326` 手写 unified engine waveform handler 注册。
- `xdebug/src/waveform/commands/cmd_ai.cpp:117` 还有 waveform CLI 的 action 白名单。
- `xdebug/src/waveform/service/action_registry.cpp:60` 维护 standalone waveform action registry。

风险：同一个 action 可能在 schema/catalog 可见，但 engine 或 standalone waveform 路径不可达；也可能 public API、MCP、旧 CLI 行为不一致。当前 contract 主要校验 runtime catalog 与 `actions.yaml` 文件一致，并不证明 engine handler registry 或 waveform registry 覆盖一致。

建议：

- 以 `actions.yaml` 或生成后的 `ActionRegistry` 为唯一真源，生成 public registry、help/catalog、MCP action 列表。
- 增加 contract：spec action、public registry、engine handler registry、waveform registry/CLI 白名单必须一致；允许显式例外表，例如 removed/deprecated/standalone-only。

### 2. 同一批 waveform action 存在 engine 与 waveform 双实现

严重度：高

证据：

- engine 侧本地实现并注册 `ValueAtHandler`、`EventHandler`、`ListExportHandler` 等：`xdebug/src/engine/service/engine_waveform_handlers.cpp:140`、`:596`、`:1193`、`:1326`。
- waveform 侧也有 `WaveformActionRegistry` 与 action wrappers：`xdebug/src/waveform/service/action_registry.cpp`、`xdebug/src/waveform/service/actions/list_actions.cpp`。
- `actions.yaml` 对这些 action 统一声明为 `handler_kind: engine_forward`，没有表达 engine 本地实现与 waveform service 实现的边界。

风险：后续维护者修改 waveform service action 时，公共 xdebug unified engine 入口可能完全不吃这套逻辑，导致行为漂移。`value.at`、`value.batch_at`、`list.*`、`event.find/export`、`verify.conditions` 这类用户高频 action 尤其容易出现双路径不一致。

建议：

- 确立唯一运行时真源。优先把业务逻辑抽成共享 service，engine/waveform handler 只做 envelope 适配。
- 如果 standalone waveform 已不再作为正式入口，应停止编译或删除旧 action router；如果仍保留，应恢复正式 target 和 contract/e2e 测试。

### 3. Session registry 边界不清，public dispatcher 与 waveform registry 可能形成 stale view

严重度：中高

证据：

- public dispatcher 使用 `SessionCatalog sessions_` 做 `session.open/list/get` 与重复 session 检查：`xdebug/src/api/dispatcher.cpp:403`、`:479`。
- `SessionCatalog` 读取 engine registry：`xdebug/src/session/session_catalog.cpp`。
- waveform 侧 session registry 使用 waveform tool config 和 `.xdebug/waveform` 命名空间：`xdebug/src/waveform/common/xdebug_waveform_paths.cpp:15`、`xdebug/src/waveform/session/session_registry.cpp:19`。

风险：`session.open`、`session.list`、`session.kill/doctor` 和 waveform 内部状态可能依赖不同 registry 视图。迁移或修复某一侧时，另一侧容易成为 stale state。

建议：

- 明确 unified engine 的 canonical session registry，并让 public dispatcher 只通过统一后端接口读取。
- 增加 session.open/list/doctor/kill 的端到端一致性测试，覆盖 design、waveform、combined 三种 session。

### 4. `trace.active_driver` 的资源合同与 handler 实际需求不一致

严重度：中高

证据：

- spec 中 `trace.active_driver` 声明 `requires: any`：`xdebug/specs/actions/actions.yaml:1409` 附近。
- engine handler 同时要求 design 和 waveform：`xdebug/src/engine/service/engine_action_registry.cpp:25`、`:28`、`:29`。

风险：API/catalog 层暗示只要有 design 或 waveform 资源即可，但运行时实际需要两者。用户或 MCP agent 可能构造出合同允许、运行时失败的请求。

建议：

- 将该 action 的 `requires` 改为 `combined` 或新增 `design_and_waveform` 语义。
- 在 validator/resource resolver 层为 combined action 做强校验，并补负例测试。

### 5. `session.close` 清理失败被吞掉并固定返回成功

严重度：中高

证据：

- `xverif_mcp/src/xverif_loop/sessions/loop_session.py:216` 到 `:247` 对 backend `session.close`、`stdio.quit`、`launcher.terminate()` 的异常均 `pass`，最后无条件返回 `{"ok": True}`。
- launchers 中部分 terminate/bkill 异常也被吞掉：`xverif_mcp/src/xverif_loop/sessions/launchers.py`。

风险：关闭失败、子进程残留、LSF job 未杀掉都会被伪装成成功，后续 session registry 和真实进程状态可能分叉。

建议：

- `close()` 返回分项 cleanup 结果：backend close、stdio quit、terminate、bkill。
- 任一关键清理失败时返回 `SESSION_CLEANUP_PARTIAL_FAILURE` 或保留 session 供重试诊断，不要直接 evict。

### 6. FSDB 变化会触发隐式 restart，绕过显式 session 生命周期

严重度：中高

证据：

- `xdebug/src/waveform/session/session_manager.cpp:636` 到 `:670` 中，`ensure_session_current()` 检测到 FSDB metadata 变化后直接 `restart_session(session_id)`。
- 普通 waveform action/client 查询会调用该检查，例如 `xdebug/src/waveform/service/action_support.cpp`、`xdebug/src/waveform/client/client.cpp`。

风险：用户以为仍在原 session 上查询，实际进程和 FSDB handle 已透明重启。这和近期显式 session 合同方向不一致，也会让复现和审计困难。

建议：

- 默认返回 `SESSION_STALE` 或 `RESOURCE_CHANGED`，要求用户显式 close/open 或 gc。
- 如果兼容性必须保留自动 restart，应 opt-in，并在响应里显式返回 `restarted:true`、旧/新 metadata。

### 7. active trace heuristic fallback 被包装成高置信度证据

严重度：中

证据：

- `xdebug/src/combined/active_trace_service.cpp:98` 到 `:110` 在 RHS AST 解析不到直接信号时，会从 `sigHdlVec` 中选择唯一候选。
- `xdebug/src/combined/active_trace_service.cpp:373` 到 `:386` 启用该 passthrough fallback。
- 后续边入队仍使用 `rhs_dependency` 与 `high` confidence：`xdebug/src/combined/active_trace_service.cpp:1064` 到 `:1071`。

风险：启发式推断会被用户误读为直接证据，尤其在 active driver 根因定位里容易扩大误判影响。

建议：

- 对 fallback 路径使用 `relation: heuristic_rhs_dependency` 或 `confidence: medium/low`。
- 在 `warnings`/`limitations` 中暴露 fallback 来源，并在报告输出中保留 evidence type。

### 8. APB/AXI/event action 会自动选择 latest config

严重度：中

证据：

- APB 缺少 `args.name` 时调用 `get_latest_apb`：`xdebug/src/waveform/service/protocol_actions.cpp:40`。
- AXI 缺少 `args.name` 时调用 `get_latest_axi`：`xdebug/src/waveform/service/protocol_actions.cpp:74`。
- event 缺少 `args.name` 时调用 `get_latest_event`：`xdebug/src/waveform/service/protocol_actions.cpp:162`。

风险：同一请求会随历史状态变化，用户可能查到错误 config。对于长 session、多协议、多 config 场景，这属于隐式状态选择。

建议：

- 对 `*.query`、`*.cursor`、`event.find/export` 强制 `args.name`，只让 `config.list` 暴露 latest 候选。
- 若保留 latest 行为，应在 response summary 中标记 `selected_by: latest` 并返回候选列表。

### 9. 旧 waveform router/server 与 mapper 疑似死代码或半保留路径

严重度：中

证据：

- `xdebug/src/backend/engine_request_mapper.cpp:6` 到 `:24` 仍实现旧 design/waveform engine request 改写，但当前 `xdebug/src/backend/engine_adapter.cpp:58` 到 `:60` 明确 unified engine 接收完整 public request，不再 strip 字段；mapper 未进入 Makefile 构建。
- `xdebug/src/waveform/service/router.cpp`、`action_registry.cpp`、`actions/*.cpp` 仍被 Makefile 编译到 unified engine 相关对象中，但公共 unified engine 主要走 `engine_*_handlers.cpp`。
- `xdebug/src/waveform/server/server.cpp`、`server/service/request_router.cpp`、`command_parser.cpp` 不在当前 Makefile 源列表中；`xdebug/src/waveform/main.cpp` 仍调用 `server_main()`，但 unified engine 链接时过滤掉 `waveform/main.o`。

风险：维护者可能修改旧 mapper/router/server 以为影响公共入口，实际无效；也会增加二进制和代码审查负担。

建议：

- 删除 `engine_request_mapper.{h,cpp}`，或至少标记 historical/unused 并移出正常源码路径。
- 对 waveform router/server 明确二选一：删除旧 standalone 入口，或恢复正式 target 并补测试。

### 10. deprecated action 缺少迁移指引

严重度：低到中

证据：

- `inspect_signal` 在 `xdebug/specs/actions/actions.yaml:742` 附近标记为 `deprecated`，仍有 schema/example。
- `xdebug/src/api/action_registry_init.cpp:130` 仍注册为 deprecated。
- `xdebug/src/api/action_spec.cpp:55` 到 `:75` 给部分 action 提供 `preferred_alternative`，但没有 `inspect_signal` 的替代指引。

风险：MCP/agent 会继续看到 deprecated action，但不知道该按场景迁移到 `value.at`、`signal.changes`、`signal.statistics` 或其他 action。

建议：

- 在 descriptor/docs 中补 `inspect_signal` 的替代路径；或进入 removed 流程，保留兼容错误和迁移说明。

## 测试评审

### 已有优势

- `schema-test`、`contract-test` 对 action spec、schema、example 文件一致性有覆盖。
- session 生命周期已有正负例：无效 session 名、`reuse/reopen` 拒绝、UDS direct timeout 不 spawn fallback。
- `run_complex_wave.py` 对 `value.at/batch_at`、X/Z、`list.export`、event aggregate、verify/window、signal statistics、AXI export 等有较多真实波形断言。
- MCP direct/fake LSF 有端到端 smoke，能覆盖基础工具发现和少数真实 action。

### 主要缺口

1. `xverif_mcp/tools/test_actions.py` 的 L2/L3 smoke 把返回 dict 计为通过，`ok:false` 只打印错误，不计失败。这会漏掉 MCP 参数映射、资源语义、action 运行错误。
2. pytest contract 中真正执行二进制的 safe examples 只覆盖 `actions/schema/batch`，大多数 stable action 的 response example 只做 schema 校验，不做语义执行。
3. unit-test 偏底层，缺少 APB/AXI/event/stream/list/cursor 等确定性逻辑的直接单元测试。
4. realdata manifest 覆盖面窄，主要是 `value.at`、`signal.changes`、`scope.list`、`trace.driver`，缺少 `event.find/export`、`list.export`、`signal.statistics`、APB/AXI/stream、combined active-driver。
5. `combined-test` 在无 NPI license 时可能 skip 并返回成功；作为 gate 名称容易误导。
6. 宽波形 regression 中仍有部分 stateful action 只是调用成功，缺少状态变化和错误码断言，例如 list 删除后内容、APB/AXI cursor begin/next、config replace/invalid config。

### 建议测试补强

- MCP smoke 默认要求 `ok:true`，只有明确负例 allowlist 接受 `ok:false`。
- 新增 executable examples：`resource:none` action 全部执行；资源类 action 通过 synthetic fixture manifest 执行一个 ok 路径和一个错误码路径。
- 增加 action registry parity 测试：public spec、engine handler、waveform registry/CLI 白名单必须一致。
- 为 `event_expr`、`stream_expr`、time range/cursor resolver、list manager、APB/AXI config validation、export metadata 增加 focused unit tests。
- 拆分 `combined-test` 与 optional license smoke；严格 gate 默认设置 `XDEBUG_REQUIRE_NPI=1`。

## 建议整改顺序

1. 先加只读 contract/audit 测试，锁住 action registry parity 和 MCP smoke `ok:true` 语义。
2. 收紧明显 fallback：`loop_session.close()` 分项返回 cleanup 失败；`ensure_session_current()` 改为 stale/resource changed；协议 latest 自动选择改为显式 name。
3. 删除或隔离旧 mapper/router/server；保留前先给 standalone target 和测试。
4. 抽共享 waveform service/context，逐步移除 engine handler 对 waveform namespace global 和 manager 细节的直接依赖。
5. 最后再做大规模目录/构建收口，避免在缺少 parity 测试时重构运行时真源。

## 本次未覆盖

- 未运行 `make -C xdebug test-*` 或 MCP/真实波形回归。
- 未审查所有 NPI/FSDB 边界的实机行为。
- 未对非 xdebug 子项目做完整评审。
