# xdebug Action 真源增强计划

日期：2026-06-25

来源：本计划对应 `xdebug/docs/XDEBUG_THIRD_PARTY_REVIEW_2026-06-25.md` 中第一项发现：Action 真源过多，新增/删除 action 容易漂移。

范围：本阶段只定义增强方案，不修改 C++/Python 运行时代码，不新增测试。本计划用于后续实现前审阅。

## 目标

将 xdebug action 的“可见性”和“可执行性”绑定起来，避免出现以下问题：

- `actions.yaml` 或 public catalog 声明了某个 action，但 engine handler 没有注册，用户请求运行时才失败。
- engine 或 waveform runtime 已经注册了某个 action，但 spec/catalog/schema/example 没有同步，MCP/CLI/help 无法可靠发现。
- standalone waveform registry、waveform `cmd_ai` 白名单、unified engine handler registry 各自维护列表，长期产生行为漂移。
- deprecated/removed action 的状态和真实 runtime 暴露不一致，导致 agent 继续选择过期 action 或用户看不到迁移指引。

## 当前真源面

当前至少存在五个需要对齐的 action 列表或 registry：

- `xdebug/specs/actions/actions.yaml`：checked-in action spec，包含 category/status/requires/handler_kind/schema/example。
- `xdebug/src/api/action_registry_init.cpp`：public `ActionRegistry` 的手写注册源，驱动 `actions` catalog、schema 查询和 dispatcher 合同。
- `xdebug/src/engine/service/engine_*_handlers.cpp`：unified engine 的真实 handler 注册源，决定 public 请求最终能否被 engine 执行。
- `xdebug/src/waveform/service/action_registry.cpp`：standalone waveform service action registry，仍维护一套 waveform action wrappers。
- `xdebug/src/waveform/commands/cmd_ai.cpp`：waveform CLI 的 `actions`/`implemented`/`action_known`/`server_ai_action` 白名单。

已有 contract 已经校验 public runtime catalog 与 `actions.yaml` 的一致性，但还不能证明 engine handler registry、standalone waveform registry 和 waveform CLI 白名单覆盖一致。

## 推荐路线

采用“护栏优先，再生成化”的两阶段路线。

第一阶段先增加可观测性和 contract gate，不改变 dispatch 行为：

- 为 engine action registry 暴露只读 action name 枚举接口，例如 `list_names()`。
- 为 waveform action registry 暴露只读 action name 枚举接口。
- 将 waveform `cmd_ai` 中的手写白名单纳入 contract 检查；短期可以通过测试解析或引入只读 helper 暴露，长期不应继续手写多份列表。
- 新增 registry parity contract，比较 `actions.yaml`、public `ActionRegistry`、engine handler registry、waveform registry、waveform CLI 白名单。
- 引入显式例外表，只允许经过说明的 `removed`、`standalone-only`、`engine-only`、`deprecated-visible`、`not-implemented-by-design` 项跳过严格一致性。

第二阶段再收敛真源：

- 以 `actions.yaml` 或生成后的 `ActionRegistry` 作为 action 元数据唯一真源。
- public catalog、help、MCP action 列表从同一份元数据派生。
- engine/waveform handler registry 不再复制 action 元数据，只声明“handler 覆盖了哪些 action”。
- waveform `cmd_ai` 的 `actions`/`implemented` 输出改为从 registry 或生成表派生，不再维护独立白名单。

不建议第一步直接大规模生成化。当前 waveform action 在 unified engine 和 standalone waveform service 中仍有双实现边界，直接生成化容易把“元数据收敛”和“运行时实现收敛”两个问题混在一起，增加回归定位成本。

## 第一阶段详细设计

### Registry 可枚举接口

新增接口只用于诊断和测试，不改变 handler 查找或请求执行路径：

- `EngineActionRegistry::list_names()` 返回已注册 engine handler action 名。
- `WaveformActionRegistry::list_names()` 返回已注册 standalone waveform handler action 名。
- 返回值排序稳定，便于测试失败时直接 diff。
- 不暴露 handler 指针、状态或内部 map，避免测试依赖实现细节。

### Parity contract

新增 contract 应检查以下关系：

- `actions.yaml` 中 `status != removed` 的 public action 必须出现在 public runtime catalog。
- `actions.yaml` 中 `status == removed` 的 action 不得出现在 engine/waveform runtime handler registry，除非例外表声明兼容入口。
- `handler_kind: engine_forward` 且 category 为 `design`、`waveform`、`combined` 的 action，必须能在 unified engine handler registry 中找到，除非例外表声明为 intentionally unimplemented。
- category 为 `waveform` 的 action，如果 standalone waveform 仍作为正式支持入口，应在 waveform registry 或 waveform CLI 白名单中有一致覆盖；如果 standalone 入口不再正式支持，应在例外表中整体标记为 historical/compatibility-only。
- deprecated action 可以继续出现在 catalog 和 handler registry，但必须有迁移说明或 preferred alternative。

测试失败信息应按 missing/extra/exception 三类输出，避免只给一个大集合 diff。

### 例外表

例外表建议使用 repo-local JSON 或 Python 常量，先服务测试，不进入 public API。每个例外项至少包含：

- `action`：action 名。
- `surface`：`engine`、`waveform_registry`、`waveform_cli`、`public_catalog` 之一。
- `reason`：短原因，例如 `standalone historical path`、`deprecated compatibility`、`removed compatibility shim`。
- `owner_decision`：保留、迁移、删除或待确认。

例外表必须小而显式。新增 action 不应默认进入例外表。

## 第二阶段详细设计

### 元数据唯一真源

优先让 `actions.yaml` 保持唯一 checked-in 元数据源：

- category/status/requires/handler_kind/schema/example 继续只在 `actions.yaml` 中维护。
- public `ActionRegistry` 可以由生成代码或加载器构造，减少 `action_registry_init.cpp` 中重复列表。
- deprecated action 的 preferred alternative 也应进入 spec 或 spec 旁路数据，避免散落在 `action_spec.cpp`。

### 运行时覆盖声明

handler registry 的职责应从“声明 action 是什么”变为“声明我实现了哪些 action”：

- engine handler registry 只注册 executable handler。
- waveform registry 只注册 standalone waveform service handler。
- public catalog 不从 handler registry 推导 action 元数据，只通过 contract 保证 handler 覆盖符合 spec。

这样可以允许某些 action 有多个 runtime adapter，但元数据仍只有一份。

### waveform CLI 白名单清理

`xdebug/src/waveform/commands/cmd_ai.cpp` 中的 `actions`、`implemented`、`action_known`、`server_ai_action` 不应长期重复维护：

- `actions` 和 `implemented` 从 waveform registry 或生成表派生。
- `server_ai_action` 如果表示路由策略，应改名为 route classification，并由测试证明它是 registry 的子集。
- 如果 standalone waveform CLI 被判定为 historical path，应在文档和测试中明确，不再把它当 public action 真源。

## 验证计划

文档阶段只需静态审阅本计划。

后续实现阶段建议分层验证：

- 运行 `make -C xdebug contract-test`，确认原有 spec/catalog/schema 合同不退化。
- 运行 `make -C xdebug pytest-contract`，确认新增 parity contract 生效。
- 增加一个故意漂移的本地验证：临时在测试分支新增 spec action 但不注册 handler，应触发 missing handler 失败；临时注册 handler 但不加 spec，应触发 extra handler 失败。
- 如果改动触及 waveform CLI 白名单，补充 CLI actions 输出检查，确认 `actions` 和 `implemented` 不再与 registry 分叉。

本计划不要求运行 FSDB/NPI/真实 LSF/MCP 回归；这些属于后续 action 行为改动或运行时重构阶段。

## 分阶段交付建议

1. 增加 registry `list_names()` 和 parity contract，只读暴露，不改 dispatch。
2. 整理并最小化例外表，把 historical/standalone/deprecated 边界写清楚。
3. 清理 waveform `cmd_ai` 白名单重复源，至少让测试覆盖它与 registry 的一致性。
4. 再评估是否生成 public `ActionRegistry`，把 `action_registry_init.cpp` 中的大型手写列表收敛掉。
5. 最后再处理 waveform/unified engine 双实现的业务逻辑共享问题，避免和 action 元数据收敛混在同一批改动。

## 明确不在本计划内

- 不改变任何 action 的 public JSON schema。
- 不改变 `session.open`、resource resolver、MCP adapter 或 xdebug dispatch 合同。
- 不删除 standalone waveform router/server。
- 不在没有 parity contract 的情况下进行大规模目录或构建重构。
