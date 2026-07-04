# xdebug Clock Sampling and Point Query Migration Plan

## Summary

- Remove `sample_offset` semantics completely. Public clock sampling inputs are `clock`, `edge`, and `sample_point`.
- Range-scan actions use the shared clock-sampling component. The default is `edge:"negedge"` and the negedge path remains the fast path.
- Point-time query actions must also require `clock`: `value.at`, `value.batch_at`, `list.value_at`, `expr.eval_at`, and `verify.conditions`.
- `trace.active_driver*`, `signal.changes`, and `cursor.*` keep their existing exact-time or active-trace semantics and are not part of this point-query clock bracket behavior.

## Public API

- Point-time actions require `args.clock`.
- `args.edge` accepts `"posedge"`, `"negedge"`, and `"dual"`; default is `"negedge"`.
- `args.sample_point` accepts `"before"` and `"after"` only when `edge` is `"posedge"` or `"dual"`; default is `"before"`.
- Passing `sample_point` with `edge:"negedge"` is an invalid request.
- `sample_offset`, `clk`, `sampling`, `clock_edge`, and `posedge` are not compatibility aliases. Requests using them must return structured `INVALID_REQUEST` errors that identify the exact invalid field and expected replacement fields.
- Point-time actions return `clock_context` with:
  - `clock`
  - `edge`
  - `requested_time`
  - `clock_edge_hit`
  - `clock_edge_kind`
  - `target_edge_hit`
  - `sample_point_applied`
  - `previous_sample_time`
  - `next_sample_time`
  - `bracket_complete`
- If `time` hits the target sampling edge, `middle` is sampled with the current mode:
  - `posedge` and `dual` use `sample_point`.
  - `negedge` uses the current fast value-at-edge semantics.
- If `time` does not hit the target sampling edge, the action returns three samples:
  - `before`: nearest previous target sampling edge.
  - `middle`: direct value at the requested time.
  - `after`: nearest next target sampling edge.
- If `time` hits a non-target clock edge, for example `edge:"posedge"` at a negedge timestamp, return `clock_edge_hit:true` and `target_edge_hit:false`, then still compute the before/middle/after bracket using the requested `edge`.

## Implementation Changes

- Update this clock sampling plan and related xdebug docs so point-time actions are clock-aware point queries, not unaffected exact-time actions.
- Refactor `ClockSampleSpec` to remove offset fields and add `sample_point`.
- Keep one parser/validator for required `clock`, default `edge`, `sample_point` validity, and legacy-field errors.
- Extend the clock helper to:
  - detect whether a timestamp is any clock edge and report the actual edge kind;
  - detect whether a timestamp is the requested target edge;
  - find previous and next target sample edges for a requested timestamp;
  - apply before/after/current sample semantics.
- Migrate range-scan actions to shared clock sampling and merged VCT/group before-after behavior. Preserve the negedge fast path without before/after overhead.
- Add a shared point sampler for point-time actions. It collects the action signal set once and produces structured `before`, `middle`, and `after` samples.
- Render XOUT tables:
  - `value.at`, `value.batch_at`, and `list.value_at`: `signal | before | middle | after`.
  - `expr.eval_at` and `verify.conditions`: first render operand signal tables, then render expression or condition result tables.
- Missing previous or next edges at waveform boundaries must not fail the action. The missing cell reports `status:"missing_edge"` and `summary.bracket_complete:false`.

## Skill and Docs

- Update the xverif skill to say AI agents should use `edge:"negedge"` by default.
- State that `edge:"posedge"` should be used only for posedge monitors, DUT posedge semantics, or sampling-boundary race analysis.
- State that `sample_point:"before"` and `"after"` can differ on posedge, especially when data changes at the same timestamp as the clock edge.
- Recommend `sample_point:"before"` by default whenever posedge is required.
- State that `sample_point:"after"` is only for inspecting post-edge waveform state.
- State that `edge:"dual"` is for special scenarios and should not be the default for ordinary valid/ready, AXI, or APB analysis.

## Test Plan

- Schema and contract tests:
  - point-time actions fail without `clock`;
  - legacy fields `sample_offset`, `clk`, `sampling`, `clock_edge`, and `posedge` fail with structured errors;
  - `sample_point` with `edge:"negedge"` fails.
- Synthetic waveform tests:
  - requested time exactly at a posedge while data changes at the same timestamp; `sample_point:"before"` reads the old value and `"after"` reads the new value;
  - requested time between two target edges returns previous, middle, and next samples;
  - default `edge:"negedge"` does not need `sample_point` and keeps the fast path;
  - `edge:"dual"` brackets against either edge;
  - requested time hits a non-target edge and reports `clock_edge_hit:true`, `target_edge_hit:false`.
- Cover JSON and XOUT table output for `value.at`, `value.batch_at`, `list.value_at`, `expr.eval_at`, and `verify.conditions`.
- Update existing waveform regressions affected by point reads so they pass `clock` explicitly.
- Grep docs and skill content to ensure they no longer recommend offset and include negedge-first, posedge-before, and before/after-difference guidance.

## Assumptions

- This is a breaking change.
- No legacy-field compatibility is kept.
- For point-time actions, `middle` is always the direct requested-time value when requested time is not a target edge.
- Previous and next edges are selected according to requested `edge`; `edge:"dual"` is the only mode that brackets against either rising or falling edges.
- `trace.active_driver*` keeps active-time and driver semantics and does not become a clock-bracket query.
