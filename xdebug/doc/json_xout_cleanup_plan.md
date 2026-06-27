# xdebug JSON 冗余治理与 XOUT 渲染架构修复计划

## 目标

本任务分三阶段完成：

1. 先修 JSON 源头冗余：备份当前所有 action 的真实 JSON 输出，从 dispatcher、engine server、handler payload 构造源头清理重复字段，保证 JSON 信息清晰、无冗余、无信息丢失；完成后做全量 JSON 复采样、对比和全仓库测试，并提交第一笔 commit。
2. 再修 XOUT 渲染架构：handler 基类只做基础渲染；所有 action 特殊追加信息只允许在各自 handler 的 `render_xout()` 中完成；完成后做全仓库测试，并提交第二笔 commit。
3. 最后审查所有 action 的 xout 输出：判断信息是否足够、action 是否有效、是否只是表达不足；输出分阶段验证报告。

硬性原则：

- 不做 fallback。
- 不在后处理层屏蔽问题。
- 不用中间转换 helper 掩盖冗余。
- 问题必须从 JSON 生成源头和渲染职责边界解决。
- 涉及 license、仿真、NPI、FSDB、真实进程通信的操作，直接在沙箱外运行。

## 第一阶段：JSON 源头治理

基线备份：

- 当前 JSON 基线保存在 `xdebug/doc/json_baseline/`。
- 修复后 JSON 输出保存在 `xdebug/doc/json_after_cleanup/`。
- `signal.search` 是 removed action，不纳入 runtime 正向采样。

JSON 返回合同：

- 顶层 `summary` 是面向快速判断的摘要。
- `data` 是详细事实载荷。
- 同一事实字段不得同时无差异存在于 `summary` 和 `data` 顶层。
- public response 不允许出现 `data.summary`。
- 派生字段只保留一个权威来源；如果必须保留派生统计字段，字段名必须表达不同语义。
- 空的 `warnings`、`findings`、`suggested_next_actions` 不默认输出。
- `truncated`、`limit`、`total_count` 必须分别表达是否截断、请求限制、截断前总数，不互相替代。
- 时间字段必须带单位；不新增无单位时间字段。

实施点：

- 调整 `dispatcher.cpp` 的 engine_forward response 合成逻辑，停止从 `data` 顶层标量自动复制生成 `summary` 后又原样保留重复内容。
- 调整 `engine_query.cpp` 和 `server.cpp` 的响应合成逻辑，去掉 `data.summary` 被保留并再次提升的重复路径。
- 清理各 handler 返回 payload 中的局部冗余，优先处理 `trace.active_driver` 和 `trace.active_driver_chain`。
- 清理 response examples 中明显 placeholder 或重复字段，使 examples 与 runtime 合同一致。

第一阶段验收：

- 全 action JSON 复采样。
- JSON baseline 对比，确认语义信息没有静默丢失。
- JSON 冗余检查直接在测试脚本中执行，有问题直接报错。
- 全仓库测试通过；需要 license/NPI/仿真/FSDB 的动作在沙箱外运行。
- 第一笔 commit 只包含 JSON 冗余治理、examples/schema/test/report 的必要更新。

## 第二阶段：XOUT 渲染架构修复

职责边界：

- handler 基类只做基础渲染。
- 基础渲染只输出通用 envelope、summary、data 中可通用表达的信息。
- 基础渲染不得根据 action 名称分支。
- 基础渲染不得包含任何具体 action 的领域语义。
- action 需要追加信息时，只能在自己的 handler `render_xout()` 中追加。
- action 特殊追加内容不得重复基础渲染已经输出的字段。

实施点：

- 移除 `xdebug/src/api/xout_renderer.cpp` 中 active driver / active driver chain 等 action-specific 分支。
- 清理之前依赖通用 renderer 特判的测试。
- 保留 `EngineActionHandler::render_xout()` 作为基础渲染入口。
- 各 action handler 如需增强 xout，自行 override `render_xout()`，先调用基类基础渲染，再追加 action-specific 信息。
- `trace.active_driver` 追加根因、active time、root driver、关键 evidence。
- `trace.active_driver_chain` 追加 chain 节点、termination、每个 hop 的信号、active time、driver/reason。

第二阶段验收：

- 静态测试确认 `xout_renderer.cpp` 不允许出现具体 action 名称特判。
- active driver / chain xout 测试确认 chain 输出完整且不重复。
- 全仓库测试通过；需要 license/NPI/仿真/FSDB 的动作在沙箱外运行。
- 第二笔 commit 只包含 xout 架构迁移、handler 渲染、xout 测试更新。

## 第三阶段：全 action XOUT review 与报告

复核 85 个 implemented action 的 xout 输出：

- xout 是否足够回答该 action 的核心问题。
- 是否存在重复字段。
- 是否存在字段名不明确。
- 是否缺少必要上下文。
- action 是否本身价值不足。
- 如果 action 看起来无效，判断原因是 action 设计无意义、JSON 信息不足、xout 表达不足、fixture 覆盖不足，还是 schema/example 与 runtime 不一致。

最终报告：

- 写入 `xdebug/doc/json_xout_cleanup_report.md`。
- 按阶段汇报工作内容、字段迁移、baseline 对比、测试命令、测试结果、commit hash。
