# xdebug Clock Sampling Breaking Migration Plan

## Summary

- Do a breaking migration for all clock-sampled xdebug actions.
- Standardize public inputs, internal names, and output summary fields around one clock sampling model.
- The shared component only resolves clock edge/sample times. It does not read data signal values, evaluate expressions, or maintain protocol state.
- Default to `edge:"negedge"` and `sample_offset:"0ns"`.
- Support `edge:"posedge"`, `edge:"negedge"`, and `edge:"dual"`.
- Keep `sample_offset:"0ns"` on the existing single-pass value-scanning path. Do not compute a shifted sample time when the offset is zero.

## Public Interface

All clock-sampled actions must use these fields:

- `clock`: clock signal path.
- `edge`: `"posedge" | "negedge" | "dual"`, default `"negedge"`.
- `sample_offset`: duration string, default `"0ns"`.

Remove the old fields from public schemas and examples:

- `clk`
- `sampling`
- `clock_edge`
- `posedge`

Old fields are not compatibility aliases. Requests that use them must fail validation with a structured `INVALID_REQUEST` response whose `invalid_arg` points at the old field and whose `expected` text tells the caller to use `clock`, `edge`, and `sample_offset`.

Persisted event, stream, AXI, and APB configs must also use the new fields. Do not add automatic migration for old config files.

## Clock Sample Time Resolver

Add a shared resolver, conceptually named `ClockSampleTimeResolver`.

The resolver owns only clock-time behavior:

- parse and hold `ClockSampleSpec`
- find target clock edges
- compute `sample_time`
- answer whether a timestamp is a target edge

It may wrap clock VCT helpers internally, because finding clock edges requires the clock waveform. It must not receive data signal handles or read data signal values.

Required internal model:

- `ClockSampleSpec.clock`
- `ClockSampleSpec.edge`: enum `{posedge, negedge, dual}`
- `ClockSampleSpec.sample_offset`
- parsed offset ticks
- `ClockSampleSpec.zero_offset`

Required resolver APIs:

- `find_next_sample(anchor_time) -> {edge_time, sample_time, edge_kind}`
- `for_each_sample_time(begin, end, callback(edge_time, sample_time, edge_kind))`
- `is_target_edge_at(time) -> bool`

`find_next_sample(anchor_time)` semantics:

- A sample is valid only when `sample_time >= anchor_time`.
- If `anchor_time` itself is the target edge and `sample_offset == 0`, return `anchor_time`.
- If `anchor_time` is a non-target edge, return the next target edge.
- If `sample_offset < 0` makes the current edge's `sample_time` earlier than `anchor_time`, skip to the next target edge.
- For `edge:"dual"`, both posedge and negedge are target edges, and the nearest valid later sample wins.

`is_target_edge_at(time)` implementation requirement:

- Use the clock VCT precise-time pattern: position the clock VCT at `time`, read back the VCT time, and only treat it as a candidate if the returned time equals `time`.
- Then compare the previous clock value and current clock value:
  - `0 -> 1`: posedge
  - `1 -> 0`: negedge
- Do not use active-trace APIs such as `npi_check_active_handle`; those are driver-activity APIs, not clock-edge APIs.

## Action Migration

Actions that need clock sampling must use the new resolver:

- `event.find`
- `event.export`
- `window.verify`
- `signal.statistics` clock mode
- `counter.statistics`
- `sampled_pulse.inspect`
- `handshake.inspect`
- stream query/export/config/load/show/list
- AXI/APB config/load/query/export/analysis

Actions that use exact times or raw value changes must not use the resolver:

- `value.at`
- `value.batch_at`
- `list.value_at`
- `expr.eval_at`
- `signal.changes`
- other exact-time cursor/value actions unless their own semantics are explicitly clock-edge based

Migration rules:

- For `sample_offset == 0`, keep each action's current single-pass data-value scan when it already has one.
- The resolver supplies edge/default/anchor semantics, not data values.
- For `sample_offset != 0`, actions may use `find_next_sample` or `for_each_sample_time`, then point-read their own data signals at `sample_time`.
- `event.find` fast path must use `find_next_sample(anchor_time)`. The action can still find candidate signal change times itself; the resolver decides the next valid clock sample time.
- `event.find` full scan with zero offset must preserve the current single-pass behavior.

## Output Contract

Every migrated action summary must include:

- `sampling_mode: "clock_edge"`
- `clock`
- `edge`
- `sample_offset`
- `sample_time_semantics: "time is sample_time"`

Row, event, transaction, and sample `time` fields must represent the actual sample time:

- `sample_offset == 0ns`: `time == edge_time`
- non-zero `sample_offset`: `time == edge_time + sample_offset`

When `sample_offset` is non-zero or `edge:"dual"` is used, rows may also include:

- `edge_time`
- `edge_kind`

Even when those explanatory fields are present, `time` remains the sample time.

## Skill And Documentation Updates

Update xverif skill guidance and xdebug docs to make the action split explicit:

- Prefer `edge:"negedge"` for waveform/protocol clock sampling.
- Use `sample_offset` only when negedge sampling does not explain monitor/skew/race behavior.
- Use `edge:"posedge"` only when an interface spec, DUT semantic, or monitor explicitly requires posedge.
- Use `edge:"dual"` only for special cases such as DDR, true dual-edge sampling, or unknown-edge protocol bring-up. Do not use dual edge for ordinary valid/ready analysis by default.
- Examples must use only `clock`, `edge`, and `sample_offset`.
- Remove old-field examples for `clk`, `sampling`, `clock_edge`, and `posedge`.

## Test Plan

Schema and contract tests:

- New fields pass: `clock`, `edge`, `sample_offset`.
- Old fields fail: `clk`, `sampling`, `clock_edge`, `posedge`.
- Missing `edge` and `sample_offset` default to `negedge + 0ns`.
- `edge:"dual"` is accepted.

Resolver tests:

- Anchor is not an edge: return the first later target edge.
- Anchor is a target edge and offset is zero: return anchor.
- Anchor is posedge and spec is negedge: return the later negedge.
- Dual-edge spec returns anchor for either posedge or negedge anchors.
- Positive offset returns `edge_time + offset`.
- Negative offset skips an edge when its `sample_time` is before the anchor.
- `is_target_edge_at` rejects timestamps that are not clock value-change times.

Action regression tests:

- `event.find` fast path covers candidate change at an edge, before an edge, and after an edge.
- `event.find` full scan with zero offset preserves current single-pass results.
- `window.verify`, `signal.statistics`, `counter.statistics`, `sampled_pulse.inspect`, and `handshake.inspect` cover negedge, posedge, dual, and offset.
- stream, AXI, and APB cover default negedge, explicit posedge, dual, positive offset, and negative offset.

Skill/doc checks:

- Grep confirms examples do not use old public fields.
- Grep confirms skill guidance says negedge first, offset/posedge only when needed, and dual edge only for special scenarios.

## Assumptions

- This is a breaking change.
- No compatibility aliases are kept.
- No automatic migration is provided for old persisted configs.
- `sample_offset` uses the existing time parser; default unit is `ns`.
- The resolver handles only clock timing and does not read business signal values.
- Zero offset remains the performance-critical path and must keep current single-pass action behavior.
