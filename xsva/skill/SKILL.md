---
name: xsva
description: >
  当 AI agent 需要把 SystemVerilog Assertion (SVA) property/assert/assume/cover
  解析成 xsva 的 Surface IR、Sequence IR、Timeline IR，生成确定性解释、Markdown、
  Mermaid/SVG 可视化，或维护 xsva golden/语义/CLI 回归时使用。适用于 SVA temporal
  semantic review、range delay/path expansion、local variable capture、first_match
  保守 lowering、unsupported/partial 边界验证；不要让 LLM 直接自由解释 SVA 原文。
---

# xsva

`xsva` 是 SVA 语义编译和解释工具。它的事实源是 IR，而不是自然语言猜测：

```text
SVA source -> Surface IR -> Sequence IR -> Timeline IR -> text/markdown/json/mermaid/svg
```

使用本 skill 时，必须优先让 `xsva` 解析和 lowering，再基于 IR 解释 SVA。不要直接对 SVA 原文自由解释 temporal semantics。

## 入口

优先使用已安装的 `xsva` 命令。如果当前 shell 没有安装命令，但工作目录是 xverif 仓库，可使用：

```bash
cd <xverif-root>/xsva
/home/yian/miniconda3/bin/python -m xsva --help
```

当前环境没有稳定的 `python` 命令时，使用 `/home/yian/miniconda3/bin/python` 或 `make test` 内置的解释器。

常用命令：

```bash
xsva list    --file input.sva
xsva scan    --file input.sva
xsva parse   --file input.sva --property p_name --emit surface-ir
xsva parse   --file input.sva --property p_name --emit sequence-ir
xsva parse   --file input.sva --property p_name --emit timeline-ir
xsva explain --file input.sva --property p_name
xsva explain --file input.sva --property p_name --markdown
xsva render  --file input.sva --property p_name --format mermaid
xsva render  --file input.sva --property p_name --format svg
```

## Agent 工作流

1. 先运行 `xsva list --file <file>`，确认 property/assertion 名称。
2. 对目标 property 先取 `timeline-ir` JSON，再解释语义。
3. 如果 timeline `lowering_status` 不是 `exact`，报告 partial/opaque/unsupported 边界，不要补完工具没有证明的语义。
4. 对 `##[m:n]` 后接 suffix sequence，检查 `match_paths` 是否展开到候选路径。
5. 对 local variable，检查 `trigger.captures`、path-specific captures 和 `depends_on_captures`。
6. 对 `first_match`、`throughout`、`intersect`、`within` 等高级 sequence，接受 conservative lowering；重点确认“不崩溃、不标 exact、不错误解释”。
7. 输出解释时引用 Timeline IR 字段：`trigger`、`obligations`、`window`、`match_paths`、`failure_conditions`、`diagnostics`。

## 语义检查速查

必须守住这些 MVP 语义：

- `req |-> ack`：`ack` 在 cycle `+0`。
- `req |=> ack`：`ack` 在 cycle `+1`。
- `req |-> ##2 ack`：`ack` 在 cycle `+2`。
- `req |=> ##2 ack`：`ack` 在 cycle `+3`。
- `req |-> ##[1:4] ack`：单个 `eventually` obligation，window `[1,4]`。
- `req |-> ##[1:3] ack ##1 done`：三条路径，`ack/done` 分别在 `+1/+2`、`+2/+3`、`+3/+4`。
- `(req, v = data) |-> ##[1:4] ack && rsp == v`：`v=data` 是 per-attempt capture，后续 obligation 依赖 `v`。
- `first_match(##[1:4] ack)`：必须保留 earliest-match 风险；未完整 lowering 时标 `partial`。

## 回归和维护

在 `<xverif-root>/xsva` 下运行：

```bash
make test
```

测试体系应包含：

- exact golden IR：`tests/golden_ir/<case>/{surface_ir,sequence_ir,timeline_ir}.json`
- direct semantics tests：检查 cycle/window/path/capture/status 字段
- CLI smoke tests：覆盖 `list/scan/parse/explain/render`

更新 xsva parser/lowering 时，先加能失败的语义测试，再修实现，最后更新 golden JSON。不要只比较 name/status/count；golden 必须做完整 JSON 对比。

## 边界

xsva 不替代 VCS、formal 或完整 SVA 仿真引擎。遇到工具未支持的 SVA 构造时，正确行为是 conservative diagnostic，而不是让 agent 补语义。

如果需要验证真实仿真 pass/fail，可在核心 case 修复后再使用 `tests/vcs` 作为第二层语义对照。
